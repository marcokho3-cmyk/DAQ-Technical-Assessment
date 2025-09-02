terraform {
  required_providers { aws = { source = "hashicorp/aws", version = "~> 5.0" } }
}
provider "aws" { region = "ap-southeast-2" }

resource "aws_s3_bucket" "weather_raw" { bucket = "rbr-weather-raw-${var.env}" }

resource "aws_s3_bucket_lifecycle_configuration" "weather_raw_lc" {
  bucket = aws_s3_bucket.weather_raw.id
  rule {
    id = "tiering"
    status = "Enabled"
    transition { days = 30 storage_class = "INTELLIGENT_TIERING" }
    transition { days = 90 storage_class = "GLACIER" }
  }
}

resource "aws_timestreamwrite_database" "weather" { database_name = "rbr_weather_${var.env}" }

resource "aws_timestreamwrite_table" "measurements" {
  database_name = aws_timestreamwrite_database.weather.database_name
  table_name    = "measurements"
  retention_properties {
    memory_store_retention_period_in_hours  = 72
    magnetic_store_retention_period_in_days = 365
  }
}
