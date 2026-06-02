#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Openterface TCP Server 测试客户端
测试所有TCP服务器功能
"""

import socket
import json
import base64
import time
import sys
from pathlib import Path
from typing import Optional, Dict, Any


class OpenterfaceTCPClient:
    """Openterface TCP客户端"""
    
    def __init__(self, host: str = "127.0.0.1", port: int = 12345):
        """
        初始化TCP客户端
        
        Args:
            host: 服务器地址
            port: 服务器端口
        """
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.connected = False
    
    def connect(self) -> bool:
        """
        连接到服务器
        
        Returns:
            bool: 连接是否成功
        """
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(30)  # 30秒超时
            self.socket.connect((self.host, self.port))
            self.connected = True
            print(f"✅ 已连接到 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"❌ 连接失败: {e}")
            self.connected = False
            return False
    
    def disconnect(self):
        """断开连接"""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            finally:
                self.socket = None
                self.connected = False
                print("🔌 已断开连接")
    
    def send_command(self, command: str) -> Optional[Dict[str, Any]]:
        """
        发送命令并接收响应
        
        Args:
            command: 要发送的命令
            
        Returns:
            Dict: 解析后的JSON响应，或None（如果失败）
        """
        if not self.connected or not self.socket:
            print("❌ 未连接到服务器")
            return None
        
        try:
            # 发送命令
            self.socket.sendall(command.encode('utf-8'))
            print(f"📤 发送命令: {command}")
            
            # 接收响应
            response_data = b""
            while True:
                chunk = self.socket.recv(4096)
                if not chunk:
                    break
                response_data += chunk
                
                # 尝试解析JSON，如果成功说明接收完整
                try:
                    json.loads(response_data.decode('utf-8'))
                    break
                except json.JSONDecodeError:
                    continue
            
            # 解析响应
            if response_data:
                response = json.loads(response_data.decode('utf-8'))
                print(f"📥 收到响应: {response.get('type', 'unknown')} - {response.get('status', 'unknown')}")
                return response
            else:
                print("⚠️ 接收到空响应")
                return None
                
        except socket.timeout:
            print("❌ 接收响应超时")
            return None
        except Exception as e:
            print(f"❌ 发送/接收错误: {e}")
            return None
    
    def get_last_image(self, save_path: Optional[str] = None) -> bool:
        """
        获取最后一张图片
        
        Args:
            save_path: 保存路径（可选）
            
        Returns:
            bool: 是否成功
        """
        print("\n🖼️ 测试：获取最后一张图片")
        response = self.send_command("lastimage")
        
        if not response:
            return False
        
        if response.get('status') == 'error':
            print(f"❌ 错误: {response.get('message', 'Unknown error')}")
            return False
        
        # 提取图片数据
        data = response.get('data', {})
        base64_image = data.get('image')
        image_format = data.get('format', 'png')
        
        if base64_image:
            # 解码base64
            image_bytes = base64.b64decode(base64_image)
            print(f"✅ 收到图片: {len(image_bytes)} 字节, 格式: {image_format}")
            
            # 保存图片
            if save_path:
                Path(save_path).write_bytes(image_bytes)
                print(f"💾 图片已保存到: {save_path}")
            
            return True
        else:
            print("❌ 响应中没有图片数据")
            return False
    
    def get_target_screen(self, save_path: Optional[str] = None) -> bool:
        """
        获取目标屏幕截图
        
        Args:
            save_path: 保存路径（可选）
            
        Returns:
            bool: 是否成功
        """
        print("\n🖥️ 测试：获取目标屏幕")
        response = self.send_command("gettargetscreen")
        
        if not response:
            return False
        
        if response.get('status') == 'error':
            print(f"❌ 错误: {response.get('message', 'Unknown error')}")
            return False
        
        # 提取屏幕数据
        data = response.get('data', {})
        base64_image = data.get('screen')
        width = data.get('width', 0)
        height = data.get('height', 0)
        
        if base64_image:
            # 解码base64
            image_bytes = base64.b64decode(base64_image)
            print(f"✅ 收到屏幕截图: {len(image_bytes)} 字节, 尺寸: {width}x{height}")
            
            # 保存图片
            if save_path:
                Path(save_path).write_bytes(image_bytes)
                print(f"💾 截图已保存到: {save_path}")
            
            return True
        else:
            print("❌ 响应中没有屏幕数据")
            return False
    
    def check_status(self) -> bool:
        """
        检查服务器状态
        
        Returns:
            bool: 是否成功
        """
        print("\n📊 测试：检查状态")
        response = self.send_command("checkstatus")
        
        if not response:
            return False
        
        status = response.get('data', {}).get('status', 'unknown')
        message = response.get('message', '')
        
        print(f"✅ 服务器状态: {status}")
        if message:
            print(f"   消息: {message}")
        
        return True
    
    def execute_script(self, script: str) -> bool:
        """
        执行脚本命令
        
        Args:
            script: 脚本内容
            
        Returns:
            bool: 是否成功
        """
        print(f"\n⚙️ 测试：执行脚本")
        print(f"   脚本内容: {script[:50]}..." if len(script) > 50 else f"   脚本内容: {script}")
        
        response = self.send_command(script)
        
        if not response:
            return False
        
        if response.get('status') == 'error':
            print(f"❌ 错误: {response.get('message', 'Unknown error')}")
            return False
        
        print(f"✅ 脚本执行成功")
        return True


def run_all_tests():
    """运行所有测试"""
    print("=" * 60)
    print("Openterface TCP 服务器测试套件")
    print("=" * 60)
    
    # 创建客户端
    client = OpenterfaceTCPClient(host="127.0.0.1", port=12345)
    
    # 连接到服务器
    if not client.connect():
        print("\n❌ 无法连接到服务器，请确保：")
        print("   1. Openterface应用正在运行")
        print("   2. TCP服务器已启动（默认端口12345）")
        return False
    
    try:
        # 测试1: 检查状态
        test1 = client.check_status()
        time.sleep(1)
        
        # 测试2: 执行简单脚本
        test2 = client.execute_script("Send Hello TCP!{Enter}")
        time.sleep(2)
        
        # 测试3: 执行组合键脚本
        test3 = client.execute_script("Send ^c")  # Ctrl+C
        time.sleep(1)
        
        # 测试4: 执行复杂脚本
        complex_script = """
Send Test Script{Enter}
Sleep 500
Send ^a
Sleep 500
Click 100, 100
"""
        test4 = client.execute_script(complex_script)
        time.sleep(2)
        
        # 测试5: 获取屏幕截图
        test5 = client.get_target_screen(save_path="test_screenshot.png")
        time.sleep(1)
        
        # 测试6: 获取最后一张图片
        test6 = client.get_last_image(save_path="test_lastimage.png")
        
        # 统计结果
        print("\n" + "=" * 60)
        print("测试结果汇总:")
        print("=" * 60)
        tests = [
            ("检查状态", test1),
            ("简单脚本执行", test2),
            ("组合键脚本", test3),
            ("复杂脚本执行", test4),
            ("获取屏幕截图", test5),
            ("获取最后图片", test6),
        ]
        
        passed = sum(1 for _, result in tests if result)
        total = len(tests)
        
        for name, result in tests:
            status = "✅ 通过" if result else "❌ 失败"
            print(f"{name:20s} {status}")
        
        print(f"\n总计: {passed}/{total} 测试通过")
        print("=" * 60)
        
        return passed == total
        
    finally:
        # 断开连接
        client.disconnect()


def interactive_mode():
    """交互模式"""
    print("=" * 60)
    print("Openterface TCP 交互模式")
    print("=" * 60)
    print("命令:")
    print("  lastimage      - 获取最后一张图片")
    print("  screen         - 获取屏幕截图")
    print("  status         - 检查状态")
    print("  script <code>  - 执行脚本")
    print("  quit           - 退出")
    print("=" * 60)
    
    client = OpenterfaceTCPClient(host="127.0.0.1", port=12345)
    
    if not client.connect():
        return
    
    try:
        while True:
            try:
                cmd = input("\n>>> ").strip()
                
                if not cmd:
                    continue
                
                if cmd.lower() == "quit":
                    break
                elif cmd.lower() == "lastimage":
                    client.get_last_image("interactive_lastimage.png")
                elif cmd.lower() == "screen":
                    client.get_target_screen("interactive_screen.png")
                elif cmd.lower() == "status":
                    client.check_status()
                elif cmd.lower().startswith("script "):
                    script_code = cmd[7:]
                    client.execute_script(script_code)
                else:
                    # 直接作为脚本执行
                    client.execute_script(cmd)
                    
            except KeyboardInterrupt:
                print("\n")
                break
            except EOFError:
                break
                
    finally:
        client.disconnect()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--interactive":
        interactive_mode()
    else:
        success = run_all_tests()
        sys.exit(0 if success else 1)
