import os
import re
from datetime import datetime

def update_version():
    version_file_path = 'resources/version.h'

    # Check if the file exists
    if not os.path.exists(version_file_path):
        print(f"Error: {version_file_path} does not exist.")
        print("Current working directory:", os.getcwd())
        print("Directory contents:", os.listdir())
        exit(1)

    # Read current version from version.h
    with open(version_file_path, 'r') as f:
        version_content = f.read()
        version_match = re.search(r'#define APP_VERSION "([^"]+)"', version_content)
        if version_match:
            version = version_match.group(1)
        else:
            print(f"Error: Version not found in {version_file_path}")
            print("File contents:")
            print(version_content)
            exit(1)

    # Split version into parts
    try:
        major, minor, patch, days = version.split('.')
    except ValueError:
        print(f"Error: Invalid version format: {version}")
        exit(1)

    # Increment patch version
    patch = str(int(patch) + 1)

    # Calculate days from start of year
    current_date = datetime.now()
    days_from_start = (current_date - datetime(current_date.year, 1, 1)).days + 1
    days = str(days_from_start).zfill(3)  # Ensure it's always 3 digits

    # Create new version string
    new_version = f"{major}.{minor}.{patch}.{days}"

    # Update version.h file
    new_version_content = re.sub(
        r'#define APP_VERSION "[^"]+"',
        f'#define APP_VERSION "{new_version}"',
        version_content
    )
    with open(version_file_path, 'w') as f:
        f.write(new_version_content)

    print(f"Updated version to {new_version}")

    # Set environment variables for use in later steps
    with open(os.environ['GITHUB_ENV'], 'a') as env_file:
        env_file.write(f"NEW_VERSION={new_version}\n")
        env_file.write(f"VERSION_FOR_INNO={new_version}\n")

if __name__ == "__main__":
    update_version()
