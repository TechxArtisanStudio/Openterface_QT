#!/usr/bin/env python3

"""
Standalone Qiniu Image Manager Script (Using Official Qiniu SDK)
Keep only N most recent images in a folder, delete old ones
"""

import os
import sys
import argparse
import warnings
from typing import Tuple, List
from datetime import datetime

# Suppress urllib3 SSL warnings on macOS with older OpenSSL
warnings.filterwarnings("ignore", message="urllib3 v2 only supports OpenSSL")

# Try to import qiniu, if not available, provide installation instructions
try:
    from qiniu import Auth, BucketManager
except ImportError as e:
    print("ERROR: qiniu package is not installed")
    print("Please install it with: pip3 install qiniu")
    print(f"Import error: {e}")
    sys.exit(1)

# Colors for output
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    NC = '\033[0m'

def log_info(msg: str):
    """Log info message"""
    print(f"{Colors.BLUE}[INFO]{Colors.NC} {msg}")

def log_success(msg: str):
    """Log success message"""
    print(f"{Colors.GREEN}[SUCCESS]{Colors.NC} {msg}")

def log_warning(msg: str):
    """Log warning message"""
    print(f"{Colors.YELLOW}[WARNING]{Colors.NC} {msg}")

def log_error(msg: str):
    """Log error message"""
    print(f"{Colors.RED}[ERROR]{Colors.NC} {msg}")

def log_item(msg: str):
    """Log item message"""
    print(f"{Colors.CYAN}  •{Colors.NC} {msg}")

def list_files_in_folder(
    access_key: str,
    secret_key: str,
    bucket: str,
    folder: str
) -> Tuple[bool, List[dict]]:
    """
    List all files in a Qiniu folder
    
    Args:
        access_key: Qiniu Access Key
        secret_key: Qiniu Secret Key
        bucket: Qiniu bucket name
        folder: Folder path (e.g., 'testresult/')
    
    Returns:
        Tuple of (success, list_of_files)
    """
    try:
        auth = Auth(access_key, secret_key)
        bucket_manager = BucketManager(auth)
        
        # Ensure folder ends with /
        if folder and not folder.endswith('/'):
            folder = folder + '/'
        
        log_info(f"Listing files in '{folder}'...")
        
        # List files with prefix
        ret, eof, info = bucket_manager.list(bucket, prefix=folder)
        
        files = []
        if 'items' in ret:
            files = ret['items']
        
        return True, files
    
    except Exception as e:
        return False, []

def delete_files(
    access_key: str,
    secret_key: str,
    bucket: str,
    keys: List[str]
) -> Tuple[bool, int, int]:
    """
    Delete multiple files from Qiniu
    
    Args:
        access_key: Qiniu Access Key
        secret_key: Qiniu Secret Key
        bucket: Qiniu bucket name
        keys: List of file keys to delete
    
    Returns:
        Tuple of (success, deleted_count, failed_count)
    """
    if not keys:
        return True, 0, 0
    
    try:
        auth = Auth(access_key, secret_key)
        bucket_manager = BucketManager(auth)
        
        deleted_count = 0
        failed_count = 0
        
        for key in keys:
            try:
                ret, resp_info = bucket_manager.delete(bucket, key)
                if resp_info.status_code == 200:
                    log_item(f"✓ Deleted: {key}")
                    deleted_count += 1
                else:
                    log_warning(f"Failed to delete {key}: HTTP {resp_info.status_code}")
                    failed_count += 1
            except Exception as e:
                log_warning(f"Failed to delete {key}: {str(e)}")
                failed_count += 1
        
        return True, deleted_count, failed_count
    
    except Exception as e:
        log_error(f"Failed to delete files: {str(e)}")
        return False, 0, len(keys)

def manage_bucket(
    access_key: str,
    secret_key: str,
    bucket: str = "openterface",
    folder: str = "testresult/",
    keep_count: int = 2
) -> Tuple[bool, str]:
    """
    Keep only N most recent images in a folder, delete old ones
    
    Args:
        access_key: Qiniu Access Key
        secret_key: Qiniu Secret Key
        bucket: Qiniu bucket name
        folder: Folder path
        keep_count: Number of recent images to keep
    
    Returns:
        Tuple of (success, message)
    """
    log_info(f"Starting bucket management...")
    log_info(f"Bucket: {bucket}")
    log_info(f"Folder: {folder}")
    log_info(f"Keep count: {keep_count}")
    
    # Create Auth object
    try:
        log_info("Creating Qiniu Auth object...")
        auth = Auth(access_key, secret_key)
        log_success("Auth object created successfully")
    except Exception as e:
        log_error(f"Failed to create Auth object: {str(e)}")
        return False, f"Failed to create Auth object: {str(e)}"
    
    # List files
    log_info(f"\nListing files in folder '{folder}'...")
    success, files = list_files_in_folder(access_key, secret_key, bucket, folder)
    
    if not success:
        log_error("Failed to list files")
        return False, "Failed to list files"
    
    if not files:
        log_warning(f"No files found in folder '{folder}'")
        return True, f"No files found in folder '{folder}'"
    
    log_success(f"Found {len(files)} file(s) in folder")
    
    # Sort files by put_time (most recent first)
    # put_time is in Unix timestamp
    sorted_files = sorted(files, key=lambda x: x.get('put_time', 0), reverse=True)
    
    log_info(f"\nFiles in bucket (sorted by date):")
    for i, file in enumerate(sorted_files):
        timestamp = file.get('put_time', 0)
        # Qiniu timestamp is in 100-nanosecond intervals
        dt = datetime.fromtimestamp(timestamp / 10000000)
        size = file.get('fsize', 0)
        size_kb = size / 1024
        status = "KEEP" if i < keep_count else "DELETE"
        status_color = Colors.GREEN if status == "KEEP" else Colors.RED
        print(f"{status_color}[{status}]{Colors.NC} {file.get('key')} ({size_kb:.1f}KB, {dt})")
    
    # Determine which files to delete
    files_to_delete = []
    if len(sorted_files) > keep_count:
        files_to_delete = [f.get('key') for f in sorted_files[keep_count:]]
    
    if not files_to_delete:
        log_success(f"\nBucket already has {keep_count} or fewer images - no deletion needed")
        return True, f"Bucket already has {keep_count} or fewer images"
    
    # Show summary
    print(f"\n{Colors.YELLOW}Summary:{Colors.NC}")
    log_item(f"Total files: {len(sorted_files)}")
    log_item(f"Files to keep: {keep_count}")
    log_item(f"Files to delete: {len(files_to_delete)}")
    
    # Confirm deletion
    if len(files_to_delete) > 0:
        print(f"\n{Colors.YELLOW}Files to be deleted:{Colors.NC}")
        for key in files_to_delete:
            log_item(key)
        
        # Ask for confirmation (unless --force is used)
        response = input(f"\n{Colors.YELLOW}Proceed with deletion? (yes/no): {Colors.NC}")
        if response.lower() not in ['yes', 'y']:
            log_warning("Deletion cancelled by user")
            return True, "Deletion cancelled by user"
    
    # Delete files
    log_info(f"\nDeleting {len(files_to_delete)} old file(s)...")
    success, deleted_count, failed_count = delete_files(
        access_key, secret_key, bucket, files_to_delete
    )
    
    if not success:
        log_error(f"Failed to delete files")
        return False, f"Failed to delete files"
    
    # Show result
    print(f"\n{Colors.GREEN}✅ Bucket management completed!{Colors.NC}")
    log_item(f"Deleted: {deleted_count} file(s)")
    if failed_count > 0:
        log_warning(f"Failed: {failed_count} file(s)")
    log_item(f"Remaining files: {keep_count}")
    
    return True, f"Deleted {deleted_count} old file(s), kept {keep_count} recent file(s)"

def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="Manage Qiniu bucket - keep only N most recent images, delete old ones",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
EXAMPLES:
  # Keep only 2 most recent images in /testresult folder (interactive)
  QINIU_AK=your_ak QINIU_SK=your_sk python3 manage_qiniu_bucket.py

  # Keep 5 most recent images (interactive)
  QINIU_AK=ak QINIU_SK=sk python3 manage_qiniu_bucket.py -k 5

  # With command line arguments
  python3 manage_qiniu_bucket.py -a your_ak -s your_sk -f testresult/ -k 2

ENVIRONMENT VARIABLES:
  QINIU_AK              Qiniu Access Key
  QINIU_SK              Qiniu Secret Key
  QINIU_BUCKET          Qiniu bucket name (default: openterface)
  QINIU_FOLDER          Folder path (default: testresult/)
  QINIU_KEEP_COUNT      Number of images to keep (default: 2)
        """
    )
    
    # Optional arguments
    parser.add_argument(
        '-a', '--ak',
        dest='access_key',
        help='Qiniu Access Key (or set QINIU_AK env var)'
    )
    
    parser.add_argument(
        '-s', '--sk',
        dest='secret_key',
        help='Qiniu Secret Key (or set QINIU_SK env var)'
    )
    
    parser.add_argument(
        '-b', '--bucket',
        dest='bucket',
        default='openterface',
        help='Qiniu bucket name (default: openterface)'
    )
    
    parser.add_argument(
        '-f', '--folder',
        dest='folder',
        default='testresult/',
        help='Folder path (default: testresult/)'
    )
    
    parser.add_argument(
        '-k', '--keep',
        dest='keep_count',
        type=int,
        default=2,
        help='Number of recent images to keep (default: 2)'
    )
    
    parser.add_argument(
        '--force',
        action='store_true',
        help='Skip confirmation prompt'
    )
    
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Enable verbose output'
    )
    
    args = parser.parse_args()
    
    # Get credentials from environment or arguments
    access_key = args.access_key or os.environ.get('QINIU_AK')
    secret_key = args.secret_key or os.environ.get('QINIU_SK')
    bucket = os.environ.get('QINIU_BUCKET') or args.bucket
    folder = os.environ.get('QINIU_FOLDER') or args.folder
    keep_count = int(os.environ.get('QINIU_KEEP_COUNT', args.keep_count))
    
    # Check if we have credentials
    if not access_key or not secret_key:
        log_error("No authentication credentials provided!")
        print()
        log_error("Please provide credentials using:")
        log_error("  1. Command line: -a ACCESS_KEY -s SECRET_KEY")
        log_error("  2. Environment variables: QINIU_AK and QINIU_SK")
        print()
        log_info("Get credentials from: https://portal.qiniu.com/")
        sys.exit(1)
    
    log_info("Using authentication method: Qiniu Official SDK")
    
    # Manage bucket
    success, result = manage_bucket(
        access_key=access_key,
        secret_key=secret_key,
        bucket=bucket,
        folder=folder,
        keep_count=keep_count
    )
    
    if success:
        print(result)
        sys.exit(0)
    else:
        print(f"Error: {result}")
        sys.exit(1)

if __name__ == '__main__':
    main()
