# OpenTerface Alpine QT ARM64 Builder

This directory contains Docker configurations for building Qt for ARM64 architecture using Alpine Linux.

## Authentication with GitHub Container Registry

Before you can push images to GitHub Container Registry (ghcr.io), you need to authenticate properly. The 403 Forbidden error indicates that there's an issue with authentication or permissions.

### Step 1: Create a GitHub Personal Access Token (PAT)

1. Go to your GitHub account settings → Developer settings → Personal access tokens
2. Click on "Generate new token" (classic)
3. Give it a descriptive name like "OpenTerface Docker Push"
4. Select the following scopes:
   - `repo` (Full control of private repositories)
   - `write:packages` (Upload packages to GitHub Package Registry)
   - `read:packages` (Download packages from GitHub Package Registry)
5. Click "Generate token"
6. **Important:** Copy the token immediately and keep it safe - you won't be able to see it again!

### Step 2: Login to GitHub Container Registry

Use your token to authenticate with GitHub Container Registry:

```bash
export GITHUB_TOKEN=your_github_personal_access_token
export GITHUB_USERNAME=your_github_username
echo $GITHUB_TOKEN | docker login ghcr.io -u $GITHUB_USERNAME --password-stdin
```

### Step 3: Build and Push the Image

Use the provided script:

```bash
chmod +x build-and-push.sh
./build-and-push.sh
```

Or manually:

```bash
docker buildx create --use --name openterface-builder --driver docker-container
docker buildx build --platform linux/arm64 \
  -t ghcr.io/kevinzjpeng/openterface-alpine-qt-arm64-builder:6.6.3-alpine \
  -f Dockerfile.alpine \
  --push \
  .
```

## Common Issues

### 403 Forbidden Error

If you encounter a "403 Forbidden" error when pushing to ghcr.io:

1. Ensure your Personal Access Token has the correct scopes (write:packages)
2. Verify that you're properly authenticated with `docker login ghcr.io`
3. Check if you have the necessary permissions to push to the repository
4. Make sure the repository exists and is properly configured to accept packages

### Repository Visibility

By default, GitHub packages inherit the visibility of their repository. If you're pushing to a repository that doesn't exist yet, make sure to create it first or adjust the visibility settings.

## Reference

- [GitHub Container Registry Documentation](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry)
- [Pushing and pulling Docker images](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry#pushing-container-images)
