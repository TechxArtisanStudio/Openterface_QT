# Docker Testing Workflows

This document describes the GitHub Actions workflows created to test the `Dockerfile.openterface-test-shared` and related Docker infrastructure.

## Workflows Overview

### 1. `docker-test.yaml` - Comprehensive Docker Testing

**Trigger Conditions:**
- Push to `main` or `dev` branches (when Docker files change)
- Pull requests to `main` or `dev` branches (when Docker files change)
- Manual dispatch with test type selection

**Test Jobs:**

#### `build-test` Job
Comprehensive testing of the Docker image:

1. **Build Verification**
   - Builds the Docker image from `Dockerfile.openterface-test-shared`
   - Verifies successful image creation
   - Inspects image metadata and configuration

2. **Container Startup Tests**
   - Tests container initialization
   - Verifies container can start without errors
   - Checks container health and basic functionality

3. **Installation Script Tests**
   - Validates the `install-openterface-shared.sh` script execution
   - Checks package installation status
   - Verifies binary placement and permissions
   - Tests launcher script creation

4. **Dependency Verification**
   - **Qt6 Dependencies**: Verifies Qt6 runtime libraries
   - **Hardware Interface**: Tests USB and udev library availability
   - **Multimedia**: Validates FFmpeg and GStreamer components

5. **Application Launcher Tests**
   - Tests the `/usr/local/bin/start-openterface.sh` script
   - Validates script syntax and executability
   - Performs dry-run testing (non-GUI mode)

6. **Hardware Simulation**
   - Tests USB device access capabilities
   - Validates udev rules and device permissions
   - Simulates Openterface hardware detection

7. **Security Assessment**
   - Checks user configuration and permissions
   - Validates sudo configuration
   - Scans for security best practices
   - Audits file permissions on critical files

8. **Performance Analysis**
   - Measures container startup time
   - Monitors resource usage
   - Analyzes Docker image size
   - Generates performance metrics

#### `integration-test` Job
End-to-end testing with real-world scenarios:

1. **Independent Script Testing**
   - Tests `install-openterface-shared.sh` in isolation
   - Validates script functionality without Docker layers

2. **Real Package Download**
   - Tests actual package download from GitHub releases
   - Verifies complete installation process
   - Validates end-to-end functionality

3. **GUI Launcher Simulation**
   - Tests the `run-openterface-docker.sh` launcher script
   - Validates help functionality
   - Tests shell mode operation

### 2. `docker-test-quick.yaml` - Fast Build Verification

**Purpose:** Quick verification for frequent development iterations

**Features:**
- Fast Docker image build test
- Basic functionality verification
- Container startup validation
- Minimal resource usage

**Trigger Conditions:**
- Push/PR to Docker-related files
- Manual dispatch
- Quick CI/CD feedback

## Usage

### Running Tests Manually

1. **Full Test Suite:**
   ```bash
   # Navigate to repository root
   cd /path/to/Openterface_QT
   
   # Trigger comprehensive test workflow
   gh workflow run docker-test.yaml
   ```

2. **Quick Test:**
   ```bash
   # Trigger quick test workflow
   gh workflow run docker-test-quick.yaml
   ```

3. **Custom Test Types:**
   ```bash
   # Run only build test
   gh workflow run docker-test.yaml -f test_type=build-only
   
   # Run only runtime tests
   gh workflow run docker-test.yaml -f test_type=run-test
   ```

### Local Testing

Before pushing changes, you can run similar tests locally:

```bash
# Build the Docker image
docker build -f docker/Dockerfile.openterface-test-shared -t openterface-test-local docker/

# Run quick functionality test
docker run --rm openterface-test-local bash -c "
  echo 'Container test...'
  whoami
  groups
  ls -la /usr/local/bin/start-openterface.sh
"

# Test with GUI launcher (requires X11)
chmod +x docker/run-openterface-docker.sh
./docker/run-openterface-docker.sh --shell
```

## Test Results and Artifacts

### Successful Test Indicators
- ✅ All test steps pass
- ✅ Docker image builds successfully
- ✅ Container starts without errors
- ✅ Openterface package installs correctly
- ✅ All dependencies are satisfied
- ✅ Hardware access permissions configured
- ✅ Launcher scripts are functional

### Generated Artifacts
- Test reports (Markdown format)
- Docker image information
- Performance metrics
- Security scan results

### Troubleshooting

#### Common Issues

1. **Docker Build Failures**
   - Check Dockerfile syntax
   - Verify base image availability
   - Review package installation errors

2. **Installation Script Errors**
   - Validate `install-openterface-shared.sh` script
   - Check GitHub release availability
   - Review package dependencies

3. **Permission Issues**
   - Verify udev rules configuration
   - Check user group assignments
   - Validate file permissions

4. **Performance Issues**
   - Monitor container resource usage
   - Check image size optimization
   - Review startup time metrics

#### Debugging Commands

```bash
# Check container logs
docker logs <container-name>

# Interactive debugging
docker run -it --rm openterface-test-shared bash

# Inspect image layers
docker history openterface-test-shared

# Test specific components
docker run --rm openterface-test-shared dpkg -l | grep openterface
```

## Integration with CI/CD

These workflows are designed to:
- Provide early feedback on Docker-related changes
- Ensure consistent testing environment
- Validate cross-platform compatibility
- Maintain security and performance standards
- Support automated release processes

## Contributing

When modifying Docker files or installation scripts:

1. Run local tests first
2. Ensure all test types pass
3. Update documentation if needed
4. Monitor workflow execution
5. Address any test failures promptly

For questions or issues, refer to the main project documentation or create an issue in the repository.
