# Alpine ARM64 Qt静态构建环境

基于Alpine Linux的轻量级ARM64交叉编译环境，用于构建Qt应用程序。此环境比基于Ubuntu的Docker镜像小得多，可加快构建和部署过程。

## 特点

- **超轻量级**: 基于Alpine Linux，镜像体积比Ubuntu小5-10倍
- **更快的下载和启动**: 容器启动速度更快，CI/CD流程更高效
- **针对ARM64优化**: 专为交叉编译ARM64应用程序设计
- **静态编译**: 生成完全静态链接的可执行文件，无外部依赖
- **缓存支持**: 缓存Qt库，加快后续构建

## 本地使用方法

```bash
# 克隆代码仓库
git clone <your-repository-url>
cd Openterface_QT/docker

# 使用Alpine环境构建
docker-compose -f docker-compose.alpine.yml up
```

完成后，应用程序可执行文件将位于项目根目录的`output`文件夹中。

## 注意事项

### Alpine的musl libc vs glibc

Alpine使用musl libc而不是传统的glibc，这在某些情况下可能导致兼容性问题。如果遇到运行时问题，可能需要考虑：

1. 确保目标系统也使用Alpine/musl libc
2. 考虑使用完全静态链接的二进制文件(-static标志)
3. 或切换回基于Ubuntu的构建环境

### 大小优势

Alpine构建的二进制文件通常比Ubuntu构建的小15-30%，主要因为musl libc的体积更小且构建选项经过了优化。

## 疑难解答

如果遇到Alpine构建问题：

1. **编译错误**: 检查是否所有必要的dev包都已安装
2. **链接错误**: Alpine的musl libc有时需要不同的链接标志，尝试添加`-static`标志
3. **运行时错误**: 确认目标平台是否支持musl libc构建的二进制文件
