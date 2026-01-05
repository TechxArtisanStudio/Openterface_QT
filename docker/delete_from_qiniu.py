#!/usr/bin/env python3

"""
Standalone Qiniu Delete Script (Using Official Qiniu SDK)
Delete uploaded files from Qiniu bucket
"""

import os
import sys
import argparse
import warnings
from typing import Tuple

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

def delete_from_qiniu(
    key: str,
    access_key: str,
    secret_key: str,
    bucket: str = "openterface"
) -> Tuple[bool, str]:
    """
    Delete file from Qiniu
    
    Args:
        key: Qiniu file key (path)
        access_key: Qiniu Access Key
        secret_key: Qiniu Secret Key
        bucket: Qiniu bucket name
    
    Returns:
        Tuple of (success, message)
    """
    log_info(f"Starting Qiniu delete process...")
    log_info(f"Bucket: {bucket}")
    log_info(f"Key: {key}")
    
    # Create Auth object
    try:
        log_info("Creating Qiniu Auth object...")
        auth = Auth(access_key, secret_key)
        log_success("Auth object created successfully")
    except Exception as e:
        log_error(f"Failed to create Auth object: {str(e)}")
        return False, f"Failed to create Auth object: {str(e)}"
    
    # Create BucketManager
    try:
        bucket_manager = BucketManager(auth)
        log_success("BucketManager created successfully")
    except Exception as e:
        log_error(f"Failed to create BucketManager: {str(e)}")
        return False, f"Failed to create BucketManager: {str(e)}"
    
    # Delete file
    try:
        log_info(f"Attempting to delete file from Qiniu...")
        ret, resp_info = bucket_manager.delete(bucket, key)
        
        log_info(f"HTTP Status: {resp_info.status_code}")
        
        if resp_info.status_code == 200:
            log_success("Delete successful!")
            log_success(f"File '{key}' has been deleted from bucket '{bucket}'")
            
            print(f"\nDelete successful!\n")
            print(f"Qiniu Key: {key}")
            print(f"🗑️  Bucket: {bucket}\n")
            
            return True, f"File '{key}' deleted successfully from bucket '{bucket}'"
        
        else:
            error_msg = resp_info.error if hasattr(resp_info, 'error') else f'HTTP {resp_info.status_code}'
            
            log_error("Delete failed")
            log_error(f"HTTP Status: {resp_info.status_code}")
            log_error(f"Error: {error_msg}")
            
            # Provide troubleshooting tips
            print(f"\nTroubleshooting Tips:")
            if resp_info.status_code == 404:
                print("  - File not found - it might already be deleted")
                print("  - Check the file key is correct")
            elif resp_info.status_code == 401:
                print("  - Authentication failed - credentials are invalid")
                print("  - Get new credentials from: https://portal.qiniu.com/")
            elif resp_info.status_code == 403:
                print("  - Access forbidden - you don't have permission to delete")
                print("  - Check bucket permissions at: https://portal.qiniu.com/")
            else:
                print("  - Check credentials validity")
                print("  - Check bucket name and permissions")
            print()
            
            return False, error_msg
    
    except Exception as e:
        log_error(f"Delete failed: {str(e)}")
        
        # Provide troubleshooting tips
        print(f"\nTroubleshooting Tips:")
        print("  - Check credentials validity")
        print("  - Check bucket name and permissions")
        print("  - Check file key format")
        print()
        
        return False, str(e)

def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="Delete a file from Qiniu using official SDK",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
EXAMPLES:
  # Delete using environment variables (recommended for CI/CD)
  QINIU_AK=your_ak QINIU_SK=your_sk python3 delete_from_qiniu.py 'uploads/20251118/1763481234599721.jpg'
  
  # Delete using command line arguments
  python3 delete_from_qiniu.py -a your_ak -s your_sk 'uploads/20251118/1763481234599721.jpg'
  
  # Delete with custom bucket
  QINIU_AK=ak QINIU_SK=sk python3 delete_from_qiniu.py -b my_bucket 'path/to/file.jpg'

ENVIRONMENT VARIABLES:
  QINIU_AK              Qiniu Access Key
  QINIU_SK              Qiniu Secret Key
  QINIU_BUCKET          Qiniu bucket name (default: openterface)
        """
    )
    
    # Positional argument
    parser.add_argument(
        'key',
        help='Qiniu file key (path) to delete'
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
        '-v', '--verbose',
        action='store_true',
        help='Enable verbose output'
    )
    
    args = parser.parse_args()
    
    # Get credentials from environment or arguments
    access_key = args.access_key or os.environ.get('QINIU_AK')
    secret_key = args.secret_key or os.environ.get('QINIU_SK')
    bucket = os.environ.get('QINIU_BUCKET') or args.bucket
    
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
    
    # Delete file
    success, result = delete_from_qiniu(
        key=args.key,
        access_key=access_key,
        secret_key=secret_key,
        bucket=bucket
    )
    
    if success:
        print(result)
        sys.exit(0)
    else:
        print(f"Error: {result}")
        sys.exit(1)

if __name__ == '__main__':
    main()
