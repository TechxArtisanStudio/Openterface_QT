import os
import re
import argparse
from datetime import datetime

def update_version(increase_version, increase_major, increase_minor):
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
    version_parts = version.split('.')
    if len(version_parts) == 3:
        # 3段式版本号 (如 "0.5.10")
        major, minor, patch = version_parts
    elif len(version_parts) == 4:
        # 4段式版本号 (如 "0.5.10.015")
        major, minor, patch, days = version_parts
    else:
        print(f"Error: Invalid version format: {version}. Expected 3 or 4 parts (major.minor.patch or major.minor.patch.days)")
        exit(1)

    # Increment major or minor version if specified
    if increase_major:
        major = str(int(major) + 1)
        minor = '0'  # Reset minor version
        patch = '0'  # Reset patch version
    elif increase_minor:
        minor = str(int(minor) + 1)
        patch = '0'  # Reset patch version

    # Increment patch version if specified
    if increase_version:
        patch = str(int(patch) + 1)

    # Git标签版本始终使用3段式
    git_tag_version = f"{major}.{minor}.{patch}"
    
    # 计算天数并创建4段式版本号用于APP_VERSION
    current_date = datetime.now()
    days_from_start = (current_date - datetime(current_date.year, 1, 1)).days + 1
    days = str(days_from_start).zfill(3)  # Ensure it's always 3 digits
    app_version = f"{major}.{minor}.{patch}.{days}"

    # Update version.h file with 4段式版本号
    new_version_content = re.sub(
        r'#define APP_VERSION "[^"]+"',
        f'#define APP_VERSION "{app_version}"',
        version_content
    )
    with open(version_file_path, 'w') as f:
        f.write(new_version_content)

    print(f"Updated APP_VERSION to {app_version}")
    print(f"Git tag version: {git_tag_version}")

    # Set environment variables for use in later steps
    if 'GITHUB_ENV' in os.environ:
        with open(os.environ['GITHUB_ENV'], 'a') as env_file:
            env_file.write(f"NEW_VERSION={app_version}\n")
            env_file.write(f"GIT_TAG_VERSION={git_tag_version}\n")
            env_file.write(f"VERSION_FOR_INNO={app_version}\n")
    else:
        # If not in GitHub Actions environment, print environment variables directly
        print(f"NEW_VERSION={app_version}")
        print(f"GIT_TAG_VERSION={git_tag_version}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Update the version in version.h')
    parser.add_argument('--increase-version', action='store_true', help='Increase the patch version number')
    parser.add_argument('--increase-major', action='store_true', help='Increase the major version number')
    parser.add_argument('--increase-minor', action='store_true', help='Increase the minor version number')
    args = parser.parse_args()

    update_version(args.increase_version, args.increase_major, args.increase_minor)

