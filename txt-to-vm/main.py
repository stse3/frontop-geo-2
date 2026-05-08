import os
import re
import logging
from datetime import datetime, timedelta
from google.cloud import bigquery

# Configure logging
logging.basicConfig(level=logging.INFO,
                   format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# BigQuery client
client = bigquery.Client()

# Configuration
BIGQUERY_SOURCE_TABLE = 'frontop-geosonic.sensor_config.vibration_data'
PROCESSED_TIMESTAMP_TABLE = 'frontop-geosonic.sensor_config.processed_timestamps'
DEVICE_DATA_DIR = 'C:\\Users\\instr_data_frontop\\Desktop\\geosonic-data\\txt-files'


 # Replace with your VM path

def sanitize_filename(device_id):
    """Sanitize device_id to be a valid filename."""
    sanitized = re.sub(r'[<>:"/\\|?*]', '_', device_id)
    return sanitized

def get_last_processed(device_id):
    """Fetch the last processed timestamp from BigQuery for the given device."""
    try:
        query = f"""
        SELECT last_processed
        FROM `{PROCESSED_TIMESTAMP_TABLE}`
        WHERE device_id = @device_id
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("device_id", "STRING", device_id)
            ]
        )
        query_job = client.query(query, job_config=job_config)
        result = query_job.result()
        
        if result.total_rows == 0:
            return None
            
        for row in result:
            return row['last_processed']
    except Exception as e:
        logger.error(f"Error fetching last processed timestamp: {str(e)}")
        return None

def update_last_processed(device_id, timestamp):
    """Update the last processed timestamp in BigQuery."""
    try:
        query = f"""
        MERGE `{PROCESSED_TIMESTAMP_TABLE}` T
        USING (SELECT @device_id as device_id, @timestamp as last_processed) S
        ON T.device_id = S.device_id
        WHEN MATCHED THEN
            UPDATE SET last_processed = S.last_processed
        WHEN NOT MATCHED THEN
            INSERT (device_id, last_processed) VALUES(device_id, last_processed)
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("device_id", "STRING", device_id),
                bigquery.ScalarQueryParameter("timestamp", "TIMESTAMP", timestamp)
            ]
        )
        query_job = client.query(query, job_config=job_config)
        query_job.result()
        logger.info(f"Updated last processed timestamp for {device_id} to {timestamp}")
    except Exception as e:
        logger.error(f"Error updating last processed timestamp: {str(e)}")

def process_device_data(device_id, lookback_hours=24):
    """Process data for a single device and save to local file."""
    try:
        last_processed = get_last_processed(device_id)
        logger.info(f"Last processed timestamp for {device_id}: {last_processed}")
        
        # Build query
        query = f"""
            SELECT 
                device_id,
                FORMAT_DATETIME('%m/%d/%y %H:%M:00', recorded_datetime) as formatted_time,
                event_number,
                vppv,
                vf,
                lppv,
                lf,
                tppv,
                tf,
                pspl_dB,
                inserted_datetime
            FROM `{BIGQUERY_SOURCE_TABLE}`
            WHERE device_id = @device_id
        """
        
        query_params = [bigquery.ScalarQueryParameter("device_id", "STRING", device_id)]
        
        if last_processed:
            query += " AND inserted_datetime > @last_processed"
            query_params.append(
                bigquery.ScalarQueryParameter("last_processed", "TIMESTAMP", last_processed)
            )
        else:
            lookback_timestamp = datetime.utcnow() - timedelta(hours=lookback_hours)
            query += " AND inserted_datetime >= @lookback_time"
            query_params.append(
                bigquery.ScalarQueryParameter("lookback_time", "TIMESTAMP", lookback_timestamp)
            )
        
        query += " ORDER BY inserted_datetime"
        
        # Execute query
        job_config = bigquery.QueryJobConfig(query_parameters=query_params)
        query_job = client.query(query, job_config=job_config)
        results = list(query_job.result())
        
        if not results:
            logger.info(f"No new data for device {device_id}")
            return 0
            
        # Prepare file path
        os.makedirs(DEVICE_DATA_DIR, exist_ok=True)
        file_path = os.path.join(DEVICE_DATA_DIR, f"{sanitize_filename(device_id)}.txt")
        
        # Write data to file
        max_timestamp = None
        with open(file_path, 'a') as f:
            for row in results:
                line = (f"{row.formatted_time} {row.event_number} {row.vppv} {row.vf} "
                       f"{row.lppv} {row.lf} {row.tppv} {row.tf} {row.pspl_dB}\n")
                f.write(line)
                max_timestamp = row.inserted_datetime
        
        # Update last processed timestamp
        if max_timestamp:
            update_last_processed(device_id, max_timestamp)
        
        return len(results)
    
    except Exception as e:
        logger.error(f"Error processing device {device_id}: {str(e)}")
        return None

def main():
    try:
        # Get list of active devices
        query = f"""
        SELECT DISTINCT device_id 
        FROM `{BIGQUERY_SOURCE_TABLE}`
        WHERE inserted_datetime >= TIMESTAMP_SUB(CURRENT_TIMESTAMP(), INTERVAL 24 HOUR)
        """
        
        query_job = client.query(query)
        device_ids = [row.device_id for row in query_job.result()]
        
        total_devices_processed = 0
        total_records_processed = 0
        
        for device_id in device_ids:
            records_processed = process_device_data(device_id)
            if records_processed:
                total_records_processed += records_processed
                total_devices_processed += 1
        
        logger.info(f"Run summary: {total_devices_processed} devices, {total_records_processed} records")
        
    except Exception as e:
        logger.error(f"Critical error: {str(e)}")
        raise

if __name__ == "__main__":
    main()