#!/usr/bin/env python3
import subprocess
import signal
import time
import sys

def test_application_exit():
    """Test the application exit behavior to check for segmentation faults."""
    
    # Set environment for offscreen rendering
    env = {
        'QT_QPA_PLATFORM': 'offscreen',
        'PATH': '/usr/bin:/bin'
    }
    
    try:
        print("Starting application...")
        
        # Start the application
        proc = subprocess.Popen(
            ['./openterfaceQT'],
            cwd='/home/bot/project/Openterface_QT/build',
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )
        
        # Let it run for a few seconds to initialize
        print("Waiting for application to initialize...")
        time.sleep(3)
        
        # Send SIGTERM to trigger clean shutdown
        print("Sending SIGTERM to trigger clean shutdown...")
        proc.terminate()
        
        # Wait for it to exit with a timeout
        print("Waiting for application to exit...")
        try:
            exit_code = proc.wait(timeout=10)
            print(f"Application exited with code: {exit_code}")
            
            if exit_code == 0:
                print("✓ SUCCESS: Application exited cleanly without segmentation fault!")
                return True
            elif exit_code == -signal.SIGTERM:
                print("✓ SUCCESS: Application terminated cleanly by SIGTERM!")
                return True
            else:
                print(f"✗ FAILURE: Application exited with unexpected code: {exit_code}")
                return False
                
        except subprocess.TimeoutExpired:
            print("Application didn't exit within timeout, forcing kill...")
            proc.kill()
            proc.wait()
            print("✗ FAILURE: Application had to be force-killed")
            return False
            
    except Exception as e:
        print(f"✗ ERROR: Exception during test: {e}")
        return False
    finally:
        # Ensure process is cleaned up
        if proc.poll() is None:
            proc.kill()
            proc.wait()

if __name__ == "__main__":
    success = test_application_exit()
    sys.exit(0 if success else 1)
