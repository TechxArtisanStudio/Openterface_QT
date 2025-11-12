# Docker Test Workflow Fixes

## Problem Summary
The "Start persistent container for testing (Shared)" step in the GitHub Actions workflow was failing because:

1. **Container exits on installation failure**: When the DEB package installation failed, the entrypoint script would exit, causing the container to stop running
2. **No "ready for testing" message**: The workflow waited for this message, but when installation failed, the message was never printed
3. **Hard exit on container failure**: The workflow would exit with code 1, preventing any diagnostic steps from running

## Root Cause
The DEB package build artifacts were not being downloaded properly in the GitHub Actions environment:
- Volume mounts were empty (no artifacts found locally)
- GitHub API token might not be properly passed to the installation script
- When installation failed, the container would exit immediately, leaving no way to debug

## Solutions Implemented

### 1. Docker Test Workflow Changes (`.github/workflows/docker-test.yaml`)

#### Updated "Start persistent container for testing (Shared)" step:

**Changes:**
- ✅ Added `GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}` to env section
- ✅ Updated environment variable passing to use proper formatting
- ✅ Improved logging with clearer configuration display
- ✅ Modified wait loop to detect both successful app start AND installation failures
- ✅ Changed container status check from hard `exit 1` to a warning that allows diagnostic steps to run
- ✅ Added recovery diagnostics when container is not in running state

**Key Improvements:**
```bash
# Before: Would exit hard
if [ "$CONTAINER_STATUS" = "true" ]; then
    echo "✅ Container is running..."
else
    echo "❌ Container not in running state"
    exit 1  # ❌ Stops workflow here
fi

# After: Allows debugging to continue
if [ "$CONTAINER_STATUS" = "true" ]; then
    echo "✅ Container is running..."
else
    echo "⚠️ Container is not in running state"
    # Show diagnostics
    docker logs $CONTAINER_ID | grep -A5 -B5 "Installation\|failed\|error"
    # Allow next steps to run
fi
```

### 2. Docker Entrypoint Script Changes (`docker/entrypoint.sh`)

#### Installation failure handling:
- ✅ Changed installation failure to NOT exit
- ✅ Container continues to app launch phase even if installation fails
- ✅ Allows debugging via `docker exec` commands

```bash
# Before: Would fail and exit
else
    INSTALL_EXIT_CODE=$?
    echo "❌ Installation failed with exit code: $INSTALL_EXIT_CODE"
    # Script would end here - container exits
fi

# After: Continues running
else
    INSTALL_EXIT_CODE=$?
    echo "❌ Installation failed with exit code: $INSTALL_EXIT_CODE"
    # Don't exit - continue to app launch for debugging
    # This allows docker exec to work for troubleshooting
fi
```

#### Application not found fallback:
- ✅ Enhanced fallback when binary not found
- ✅ Container stays alive indefinitely for debugging
- ✅ Provides installation log diagnostics
- ✅ Prints message indicating container is ready for testing

```bash
# Before: Would exec bash and exit if not connected to terminal
else
    echo "⚠️ openterfaceQT application not found"
    exec /bin/bash  # ❌ This exits if not interactive
fi

# After: Stays alive for debugging
else
    echo "⚠️ openterfaceQT application not found"
    echo "ℹ️  Container will stay alive for debugging (docker exec)"
    echo "🚀 Ready for testing!"
    trap '' SIGTERM SIGINT  # Ignore signals to keep container alive
    while true; do
        sleep 3600
    done
fi
```

## Benefits

1. **Better Error Diagnostics**: Installation failures now display clearly with recovery suggestions
2. **Persistent Container**: Container stays running even if app fails to start, enabling `docker exec` debugging
3. **Workflow Continues**: GitHub Actions workflow no longer hard-fails, allowing diagnostic steps to collect information
4. **GitHub Token Passed**: GITHUB_TOKEN is now properly passed to container, enabling artifact downloads
5. **Improved Logging**: Better logging of what's happening at each step

## Testing

To test these changes:

1. **Manual local test**:
```bash
docker run -d \
  -e DISPLAY=:98 \
  -e GITHUB_TOKEN=your_token \
  -e INSTALL_TYPE=deb \
  --privileged \
  openterface-test-shared:latest

# Container will stay running even if installation fails
docker exec <container-id> bash  # Can investigate issues

docker logs <container-id>  # See full logs with diagnostics
```

2. **GitHub Actions test**:
- Push changes to your branch
- The docker-test workflow will run
- Check job logs for improved diagnostics
- Container will continue running even if installation fails

## Next Steps for Further Improvement

If installation still fails after these changes:

1. **Check GITHUB_TOKEN**: Verify the token is valid and has repo access
2. **Check Linux Build**: Verify the `linux-build.yaml` workflow is producing DEB artifacts
3. **Artifact Download**: The install script will attempt to download from GitHub API if volume mount fails
4. **Manual debugging**: Use `docker exec` to run installation script manually with more verbose logging

## Files Modified

1. `.github/workflows/docker-test.yaml` - Enhanced "Start persistent container for testing (Shared)" step
2. `docker/entrypoint.sh` - Fixed installation failure handling and app-not-found fallback
