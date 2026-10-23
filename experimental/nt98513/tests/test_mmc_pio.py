"""Execute the production MMC state machine against inert MMIO and time."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / 'patches/linux/0010-nt98513-mmc-pio.patch'


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def added_file(text, path):
    marker = '+++ b/' + path + '\n'
    section = text.split(marker, 1)[1].split('\n--- ', 1)[0]
    return ''.join(line[1:] for line in section.splitlines(keepends=True)
                   if line.startswith('+') and not line.startswith('+++'))


class MMCPIO(unittest.TestCase):
    def test_production_request_irq_worker_lifetime(self):
        self.assertTrue(PATCH.is_file(), 'MMC PIO production implementation absent')
        text = PATCH.read_text()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('nt98513-mmc.h', 'nt98513-mmc-core.c'):
                (root / name).write_text(added_file(text, 'drivers/mmc/host/nt98513/' + name))
            driver = added_file(text, 'drivers/mmc/host/nt98513/nt98513-mmc-driver.c')
            signatures = ('static unsigned nt_host_read(', 'static void nt_host_write(',
                          'static void nt_disable_irq(', 'static void nt_host_sync_irq(',
                          'static int nt_host_copy(', 'static void nt_host_done(',
                          'static void nt_work(', 'static irqreturn_t nt_irq(',
                          'static void nt_request(', 'static void nt_set_ios(',
                          'static int nt_resources(', 'static void nt_retire(', 'static int nt_probe(')
            (root / 'nt-adapter.inc').write_text('\n'.join(function(driver, sig) for sig in signatures))
            exe = root / 'mmc-pio'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), str(HERE / 'mmc_pio.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=20)
            originals = {name: (root / name).read_text() for name in
                         ('nt98513-mmc-core.c', 'nt-adapter.inc')}
            controls = [
                ('physical-mmio-ignores-fault', 'nt-adapter.inc',
                 'if (!READ_ONCE(h->core.fault))', 'if (1)'),
                ('physical-mmio-not-irq-excluded', 'nt-adapter.inc',
                 'local_irq_save(flags);\n\tif (!READ_ONCE(h->core.fault))',
                 'flags = 0;\n\tif (!READ_ONCE(h->core.fault))'),
                ('control-read-fault-not-published', 'nt-adapter.inc',
                 'h->core.fault = -EIO;', '(void)0;'),
                ('normal-drain-unlock-before-mask', 'nt98513-mmc-core.c',
                 'c->irq_access = 0;\n\tnt_write(c, 0x30, 0);',
                 'c->irq_access = 0;\n\tc->ops->irq_unlock(c->context, flags);\n\tflags = c->ops->irq_lock(c->context);\n\tnt_write(c, 0x30, 0);'),
                ('fault-handler-ignores-fenced-level', 'nt-adapter.inc',
                 '/* A process-side fault may have fenced MMIO just before this IRQ ran. */\n\tif (READ_ONCE(h->core.fault))',
                 '/* Mutation: do not recognize a fault at IRQ entry. */\n\tif (0)'),
                ('latch-not-irq-excluded', 'nt-adapter.inc',
                 'local_irq_save(flags);\n\tif (READ_ONCE(h->irq_live)',
                 'flags = 0;\n\tif (READ_ONCE(h->irq_live)'),
                ('poll-fault-not-published-before-unlock', 'nt98513-mmc-core.c',
                 'if (status == ~0U) {\n\t\tc->fault = -EIO;\n\t\tc->irq_access = 0;\n\t\tc->pending |= NT_READ_FAULT;',
                 'if (status == ~0U) {\n\t\tc->pending |= NT_READ_FAULT;'),
                ('of-resource-count', 'nt-adapter.inc',
                 'pdev->num_resources != 1', 'pdev->num_resources != 2'),
                ('fault-irq-not-disabled', 'nt-adapter.inc',
                 'disable_irq_nosync(h->irq);', '(void)disable_irq_nosync;'),
                ('irq-disable-depth', 'nt-adapter.inc',
                 '!atomic_cmpxchg(&h->irq_disabled, 0, 1)',
                 '(!atomic_cmpxchg(&h->irq_disabled, 0, 1) || 1)'),
                ('readiness-past-deadline', 'nt98513-mmc-core.c',
                 'if (c->ops->now_us(c->context) >= command_deadline) {', 'if (0) {'),
                ('final-read-past-deadline', 'nt98513-mmc-core.c',
                 'if (!error && c->ops->now_us(c->context) >= command_deadline)', 'if (0)'),
                ('data40-command', 'nt98513-mmc-core.c',
                 '(r->length ? 0x10 : 0x50)', '0x50'),
                ('tail-write-without-ready', 'nt98513-mmc-core.c',
                 'if (!(value & 0x100))', 'if (0)'),
                ('callback-under-lock', 'nt98513-mmc-core.c',
                 'c->active = NULL;\n\tc->ops->unlock(c->context);\n\t/* Callback can free r and synchronously submit the next request. */\n\tc->ops->done(c->context, r);',
                 'c->active = NULL;\n\tc->ops->done(c->context, r);\n\tc->ops->unlock(c->context);'),
                ('irq-after-clockoff', 'nt98513-mmc-core.c',
                 'if (!c->irq_access) {', 'if (0) {'),
                ('stale-pending', 'nt98513-mmc-core.c',
                 'nt_write(c, 0x28, NT_STATUS_MASK);\n\t\tc->pending = 0;',
                 'nt_write(c, 0x28, NT_STATUS_MASK);'),
                ('dma-bit', 'nt98513-mmc-core.c',
                 'r->write ? 5 : 1', 'r->write ? 7 : 3'),
                ('skip-clock-admission', 'nt-adapter.inc',
                 'error = nt98513_mmc_clock_admit(h->clock);', '(void)nt98513_mmc_clock_admit; error = 0;'),
                ('clock-fault-without-irq-fence', 'nt-adapter.inc',
                 'nt_mmc_irq_off(&h->core);', '(void)0;'),
            ]
            for label, name, old, replacement in controls:
                with self.subTest(control=label):
                    self.assertIn(old, originals[name])
                    for path, value in originals.items():
                        (root / path).write_text(value)
                    (root / name).write_text(originals[name].replace(old, replacement))
                    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(root), str(HERE / 'mmc_pio.c'), '-o', str(exe)], check=True)
                    rejected = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
                    self.assertNotEqual(rejected.returncode, 0, label)
                    self.assertIn('Assertion', rejected.stderr)
                    print('MMC production negative control rejected:', label)


if __name__ == '__main__':
    unittest.main()
