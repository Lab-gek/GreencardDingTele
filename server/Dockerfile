FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY ingest.py .
COPY config.yaml .

CMD ["python", "ingest.py", "--config", "config.yaml"]
