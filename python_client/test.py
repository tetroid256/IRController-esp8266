import socket
import struct
import json
import os

ESP8266_IP = "192.168.1.86" 
UDP_PORT = 5001
DATA_FILE = "ir_data.json"

def load_ir_database():
    """JSONファイルから赤外線データを読み込む"""
    if not os.path.exists(DATA_FILE):
        print(f"エラー: データファイル '{DATA_FILE}' が見つかりません。")
        return {}
    
    with open(DATA_FILE, "r", encoding="utf-8") as f:
        return json.load(f)

def send_command(action_name):
    """指定したアクション名の赤外線データをESP8266に送信する"""
    ir_db = load_ir_database()
    
    if action_name not in ir_db:
        print(f"エラー: '{action_name}' はデータベースに登録されていません。")
        return

    raw_array = ir_db[action_name]
    
    # リトルエンディアンで16ビット符号なし整数（2バイト）のバイナリに変換
    fmt = f"<{len(raw_array)}H"
    binary_data = struct.pack(fmt, *raw_array)
    
    print(f"[{action_name}] のパケットを送信します (サイズ: {len(binary_data)} Bytes)")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(binary_data, (ESP8266_IP, UDP_PORT))
        print("送信完了！")

if __name__ == "__main__":
    send_command("turn_on")
    # send_command("turn_off")