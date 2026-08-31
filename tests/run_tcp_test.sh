#!/bin/bash
# ============================================
# Openterface TCP 服务测试启动脚本
# ============================================

echo "============================================"
echo "Openterface TCP 服务测试"
echo "============================================"
echo ""

# 检查Python是否安装
if ! command -v python3 &> /dev/null; then
    echo "[错误] 未检测到Python3，请先安装Python 3.6+"
    exit 1
fi

echo "[信息] Python版本:"
python3 --version
echo ""

# 选择测试模式
echo "请选择测试模式:"
echo "  1. 自动测试 (运行所有测试用例)"
echo "  2. 交互模式 (手动输入命令)"
echo ""
read -p "请输入选项 (1/2): " choice

if [ "$choice" = "1" ]; then
    echo ""
    echo "============================================"
    echo "正在运行自动测试..."
    echo "============================================"
    python3 tcp_client_test.py
elif [ "$choice" = "2" ]; then
    echo ""
    echo "============================================"
    echo "进入交互模式..."
    echo "提示: 输入 'quit' 退出"
    echo "============================================"
    python3 tcp_client_test.py --interactive
else
    echo "[错误] 无效的选项"
    exit 1
fi

echo ""
echo "============================================"
echo "测试完成"
echo "============================================"
