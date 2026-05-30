import socket
import json

# ==========================================
# 先ほどシリアルモニターで確認したIPアドレスに変更してください
ESP8266_IP = "192.168.1.86" 
# ==========================================
UDP_PORT = 5001

# 送信するエアコンの命令
command = {
    "power": True,     # 電源ON
    "mode": "cool",    # 冷房
    "temp": 26         # 25度
}

def send_command():
    # JSON文字列に変換してエンコード
    message = json.dumps(command).encode('utf-8')
    
    # UDPソケットを作成して送信
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(message, (ESP8266_IP, UDP_PORT))
        print(f"送信完了: {command}")

if __name__ == "__main__":
    send_command()