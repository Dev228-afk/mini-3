
import argparse
import csv
import random
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Iterable, List, Tuple, Optional

# format is used accoriding to fire_dataset
START_OF_2020 = datetime(2020, 1, 1)
HOURS_IN_2020 = 366 * 24  # leap year

PARAMETERS = ["OZONE", "PM2.5", "PM10", "CO", "NO2", "SO2"]
UNITS = ["PPB", "UG/M3", "PPM"]

SITE_NAMES = [
    "16th and Whitmore",
    "Downtown Monitor",
    "Riverside Station",
    "Industrial Park",
    "Suburban Center",
    "Airport Site",
    "North District",
    "South Valley",
]

AGENCIES = [
    "Douglas County Health Department (Omaha)",
    "EPA Regional Office",
    "State Environmental Agency",
    "City Air Quality Division",
]


@dataclass
class AirQualityRecord:
    latitude: float
    longitude: float
    utc_timestamp: str
    parameter: str
    concentration: int
    unit: str
    raw_concentration: int
    aqi: int
    category: int
    site_name: str
    site_agency: str
    aqs_id: int
    full_aqs_id: str

    def as_row(self) -> List[str]:
        return [
            f"{self.latitude:.6f}",
            f"{self.longitude:.6f}",
            self.utc_timestamp,
            self.parameter,
            str(self.concentration),
            self.unit,
            str(self.raw_concentration),
            str(self.aqi),
            str(self.category),
            self.site_name,
            self.site_agency,
            str(self.aqs_id),
            self.full_aqs_id,
        ]



def random_coordinate() -> Tuple[float, float]:
    lat = random.uniform(-90, 90)
    lon = random.uniform(-180, 180)
    return lat, lon


def random_2020_utc_string() -> str:
    """Return a random hour in 2020 formatted like '1/3/20 14:00'."""
    offset_hours = random.randint(0, HOURS_IN_2020 - 1)
    dt = START_OF_2020 + timedelta(hours=offset_hours)
    # Use a more portable strftime (no %-m / %-d)
    month = dt.month
    day = dt.day
    year_short = dt.year % 100
    hour = dt.hour
    return f"{month}/{day}/{year_short} {hour}:00"


def pick_parameter_and_unit() -> Tuple[str, str]:
    parameter = random.choice(PARAMETERS)
    unit = random.choice(UNITS)
    return parameter, unit


def random_concentrations() -> Tuple[int, int, int]:
    raw = random.randint(0, 100)
    concentration = int(raw * random.uniform(1.05, 1.25))
    aqi = random.randint(0, 200)
    return raw, concentration, aqi


def random_site_info() -> Tuple[str, str]:
    return random.choice(SITE_NAMES), random.choice(AGENCIES)


def random_ids() -> Tuple[int, str]:
    aqs_id = random.randint(100_000_000, 999_999_999)
    prefix = random.randint(840000000000, 849999999999)
    full_aqs_id = f"{prefix:012d}"
    return aqs_id, full_aqs_id


def generate_record() -> AirQualityRecord:
    lat, lon = random_coordinate()
    utc_timestamp = random_2020_utc_string()
    parameter, unit = pick_parameter_and_unit()
    raw, concentration, aqi = random_concentrations()
    category = random.randint(1, 5)
    site_name, site_agency = random_site_info()
    aqs_id, full_aqs_id = random_ids()

    return AirQualityRecord(
        latitude=lat,
        longitude=lon,
        utc_timestamp=utc_timestamp,
        parameter=parameter,
        concentration=concentration,
        unit=unit,
        raw_concentration=raw,
        aqi=aqi,
        category=category,
        site_name=site_name,
        site_agency=site_agency,
        aqs_id=aqs_id,
        full_aqs_id=full_aqs_id,
    )


def iter_records(count: int) -> Iterable[AirQualityRecord]:
    for _ in range(count):
        yield generate_record()

HEADERS = [
    "Latitude",
    "Longitude",
    "UTC",
    "Parameter",
    "Concentration",
    "Unit",
    "Raw Concentration",
    "AQI",
    "Category",
    "Site Name",
    "Site Agency",
    "AQS ID",
    "Full AQS ID",
]


def write_csv(filename: str, num_rows: int) -> None:
    print(f"Generating {num_rows:,} rows into {filename!r}...")

    with open(filename, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(HEADERS)

        for i, record in enumerate(iter_records(num_rows), start=1):
            writer.writerow(record.as_row())

            if i % 10_000 == 0 or i == num_rows:
                pct = (i / num_rows) * 100
                print(f"Progress: {pct:5.1f}% ({i:,}/{num_rows:,})", end="\r")

    print(f"\nDone. Wrote {num_rows:,} rows to {filename}.")

SIZE_MAP = {
    "1K": 1_000,
    "5K": 5_000,
    "10K": 10_000,
    "50K": 50_000,
    "100K": 100_000,
    "200K": 200_000,
    "500K": 500_000,
    "1M": 1_000_000,
    "5M": 5_000_000,
    "10M": 10_000_000,
}


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate CSV files with synthetic air quality monitoring data.",
    )

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--rows", type=int, help="Number of rows to generate.")
    group.add_argument(
        "--size",
        type=str,
        choices=SIZE_MAP.keys(),
        help="Predefined size shortcut (1K, 10K, 100K, 1M, 10M).",
    )

    parser.add_argument(
        "--output",
        type=str,
        help="Output filename (default: air_quality_data_<rows>_rows.csv).",
    )

    return parser.parse_args(argv)


def effective_row_count(args: argparse.Namespace) -> int:
    if args.size:
        return SIZE_MAP[args.size]
    if args.rows and args.rows > 0:
        return args.rows
    raise ValueError("You must specify a positive --rows value or a valid --size.")


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)

    try:
        num_rows = effective_row_count(args)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 2

    filename = args.output or f"air_quality_data_{num_rows}_rows.csv"

    try:
        write_csv(filename, num_rows)
        return 0
    except KeyboardInterrupt:
        print("\nGeneration cancelled by user.")
        return 1
    except Exception as e:
        print(f"\nUnexpected error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
