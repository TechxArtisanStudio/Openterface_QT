#!/usr/bin/env python3

"""
Standalone Qiniu Upload Script (Using Official Qiniu SDK)
"""

import os
import sys
import argparse
import warnings
from pathlib import Path
from datetime import datetime
from typing import Tuple

# Suppress urllib3 SSL warnings on macOS with older OpenSSL
warnings.filterwarnings("ignore", message="urllib3 v2 only supports OpenSSL")

# Try to import qiniu, if not available, provide installation instructions
try:
    from qiniu import Auth, put_file, put_file_v2, put_data
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

def validate_file(file_path: str) -> Tuple[bool, str]:
    """
    Validate if file exists and is readable
    
    Args:
        file_path: Path to the file
    
    Returns:
        Tuple of (is_valid, error_message)
    """
    path = Path(file_path)
    
    if not path.exists():
        return False, f"File not found: {file_path}"
    
    if not path.is_file():
        return False, f"Path is not a file: {file_path}"
    
    if not path.stat().st_size:
        return False, f"File is empty: {file_path}"
    
    if not os.access(file_path, os.R_OK):
        return False, f"File is not readable: {file_path}"
    
    return True, ""

def upload_to_qiniu(
    file_path: str,
    access_key: str,
    secret_key: str,
    bucket: str = "openterface",
    domain: str = "download.openterface.com",
    timeout: int = 120
) -> Tuple[bool, str]:
    """
    Upload file to Qiniu using official SDK
    
    Args:
        file_path: Path to file to upload
        access_key: Qiniu Access Key
        secret_key: Qiniu Secret Key
        bucket: Qiniu bucket name
        domain: Domain for the URL
        timeout: Request timeout in seconds
    
    Returns:
        Tuple of (success, url_or_error_message)
    """
    # Validate file
    is_valid, error_msg = validate_file(file_path)
    if not is_valid:
        return False, error_msg
    
    path = Path(file_path)
    file_size = path.stat().st_size
    file_size_human = f"{file_size / 1024:.1f}K" if file_size < 1024*1024 else f"{file_size / (1024*1024):.1f}M"
    
    log_info(f"File validated: {file_path} ({file_size_human})")
    
    # Check file size
    if file_size > 2097152:
        log_warning(f"File size ({file_size} bytes) is large (Qiniu recommends max 2MB)")
    
    log_info(f"Starting Qiniu upload process...")
    log_info(f"File: {file_path}")
    log_info(f"Bucket: {bucket}")
    log_info(f"Domain: {domain}")
    log_info(f"File size: {file_size_human} ({file_size} bytes)")
    
    # Create Auth object
    try:
        log_info("Creating Qiniu Auth object...")
        auth = Auth(access_key, secret_key)
        log_success("Auth object created successfully")
    except Exception as e:
        log_error(f"Failed to create Auth object: {str(e)}")
        return False, f"Failed to create Auth object: {str(e)}"
    
    # Generate upload key
    now = datetime.now()
    timestamp = int(now.timestamp() * 1000000)  # microseconds
    ext = path.suffix or ".bin"
    qiniu_key = f"uploads/{now.strftime('%Y%m%d')}/{timestamp}{ext}"
    
    log_info(f"Using Qiniu key: {qiniu_key}")
    
    # Generate upload token
    try:
        log_info("Generating upload token...")
        
        # Upload policy
        policy = {
            # Optional: add callback, persistent operations, etc.
        }
        
        # Generate token with 3600 seconds (1 hour) validity
        token = auth.upload_token(bucket, qiniu_key, 3600, policy)
        log_success("Upload token generated successfully")
        log_info(f"Token length: {len(token)} characters")
    except Exception as e:
        log_error(f"Failed to generate upload token: {str(e)}")
        return False, f"Failed to generate upload token: {str(e)}"
    
    # Upload file
    try:
        log_info("Attempting upload to Qiniu...")
        
        # Upload file using put_file_v2 (recommended)
        ret, info = put_file_v2(token, qiniu_key, file_path)
        
        log_info(f"HTTP Status: {info.status_code}")
        
        if info.status_code == 200:
            log_success("Upload successful!")
            
            hash_val = ret.get('hash', '')
            key_val = ret.get('key', qiniu_key)
            
            log_info(f"Hash: {hash_val}")
            log_info(f"Key: {key_val}")
            
            # Construct final URL
            final_url = f"https://{domain}/{key_val}"
            
            log_success(f"Final URL: {final_url}")
            
            print(f"\n🔗 IMAGE URL: {final_url}\n")
            print(f"📦 Qiniu Key: {key_val}")
            print(f"🔐 Hash: {hash_val}\n")
            
            return True, final_url
        
        else:
            error_msg = info.error if hasattr(info, 'error') else 'Unknown error'
            
            log_error("Upload failed")
            log_error(f"HTTP Status: {info.status_code}")
            log_error(f"Error: {error_msg}")
            
            # Provide troubleshooting tips
            print(f"\n🔧 Troubleshooting Tips:")
            if info.status_code == 400:
                print("  - The upload token might be invalid or expired")
                print("  - Check your upload token at: https://portal.qiniu.com/")
                print("  - The image format might not be supported")
                print("  - Try with a smaller image file")
            elif info.status_code == 401:
                print("  - Authentication failed - credentials are invalid")
                print("  - Get new credentials from: https://portal.qiniu.com/")
            elif info.status_code == 413:
                print("  - Image file is too large for Qiniu")
                print("  - Try with a smaller image (max 2MB)")
            elif info.status_code == 429:
                print("  - Too many requests - rate limit exceeded")
                print("  - Wait a few minutes and try again")
            elif info.status_code == 403:
                print("  - Access forbidden - your IP might be blocked")
                print("  - Try using a VPN or different network")
            else:
                print("  - Check internet connection")
                print("  - Try with different credentials")
            print()
            
            return False, error_msg
    
    except Exception as e:
        log_error(f"Upload failed: {str(e)}")
        
        # Provide troubleshooting tips
        print(f"\n🔧 Troubleshooting Tips:")
        if "timeout" in str(e).lower():
            print(f"  - Upload timed out after {timeout} seconds")
            print("  - Try increasing timeout with --timeout parameter")
        elif "connection" in str(e).lower():
            print("  - Connection error - check your internet connection")
            print("  - Qiniu API might be temporarily unavailable")
        else:
            print("  - Check credentials validity")
            print("  - Check bucket name and permissions")
        print()
        
        return False, str(e)

def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="Upload an image to Qiniu using official SDK and get the shareable URL",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
EXAMPLES:
  # Using environment variables (recommended for CI/CD)
  QINIU_AK=your_ak QINIU_SK=your_sk python3 upload_to_qiniu.py photo.jpeg
  
  # Using command line arguments
  python3 upload_to_qiniu.py -a your_ak -s your_sk photo.jpeg
  
  # With custom domain
  QINIU_AK=ak QINIU_SK=sk python3 upload_to_qiniu.py -d download.openterface.com photo.jpeg

ENVIRONMENT VARIABLES:
  QINIU_AK              Qiniu Access Key
  QINIU_SK              Qiniu Secret Key
  QINIU_BUCKET          Qiniu bucket name (default: openterface)
  QINIU_DOMAIN          Your Qiniu domain (default: download.openterface.com)
        """
    )
    
    # Positional argument
    parser.add_argument(
        'image_file',
        help='Path to the image file to upload'
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
        '-d', '--domain',
        dest='domain',
        default='download.openterface.com',
        help='Qiniu domain for URLs (default: download.openterface.com)'
    )
    
    parser.add_argument(
        '-b', '--bucket',
        dest='bucket',
        default='openterface',
        help='Qiniu bucket name (default: openterface)'
    )
    
    parser.add_argument(
        '--timeout',
        type=int,
        default=120,
        help='Request timeout in seconds (default: 120)'
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
    domain = os.environ.get('QINIU_DOMAIN') or args.domain
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
    
    # Upload file
    success, result = upload_to_qiniu(
        file_path=args.image_file,
        access_key=access_key,
        secret_key=secret_key,
        bucket=bucket,
        domain=domain,
        timeout=args.timeout
    )
    
    if success:
        print(result)
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == '__main__':
    main()
