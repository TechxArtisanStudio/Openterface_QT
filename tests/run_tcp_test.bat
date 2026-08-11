@echo off
REM ============================================
REM Openterface TCP 服务测试启动脚本
REM ============================================

echo ============================================
echo Openterface TCP 服务测试
echo ============================================
echo.

REM 检查Python是否安装
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未检测到Python，请先安装Python 3.6+
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo [信息] Python版本:
python --version
echo.

REM 选择测试模式
echo 请选择测试模式:
echo   1. 自动测试 (运行所有测试用例)
echo   2. 交互模式 (手动输入命令)
echo.
set /p choice="请输入选项 (1/2): "

if "%choice%"=="1" (
    echo.
    echo ============================================
    echo 正在运行自动测试...
    echo ============================================
    python tcp_client_test.py
) else if "%choice%"=="2" (
    echo.
    echo ============================================
    echo 进入交互模式...
    echo 提示: 输入 'quit' 退出
    echo ============================================
    python tcp_client_test.py --interactive
) else (
    echo [错误] 无效的选项
    pause
    exit /b 1
)

echo.
echo ============================================
echo 测试完成
echo ============================================
pause
