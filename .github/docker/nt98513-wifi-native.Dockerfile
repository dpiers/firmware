# Public official Debian base; no locally retained image is a CI prerequisite.
FROM debian@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587
RUN apt-get -o Acquire::Retries=2 -o Acquire::http::Timeout=30 update \
    && apt-get install -y --no-install-recommends build-essential python3
ENV LC_ALL=C LANG=C TZ=UTC PYTHONDONTWRITEBYTECODE=1
# Acquisition above may use networking. The workflow disables it for every test.
