"""Generate a local rename SysEx; no third-party MIDI library needed."""
import argparse
from pathlib import Path

def make_command(name):
    data = name.encode('ascii')
    if not 1 <= len(data) <= 29 or not name.strip() or any(b < 32 or b > 126 for b in data):
        raise ValueError('Name must be 1..29 printable ASCII bytes, not all spaces')
    return bytes.fromhex('F0 7D 45 53 50 01 01') + data + bytes([0xF7])

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('name')
    parser.add_argument('--output', type=Path, help='Write binary .syx file')
    args = parser.parse_args()
    try:
        command = make_command(args.name)
    except (ValueError, UnicodeEncodeError) as e:
        parser.error(str(e))
    if args.output:
        args.output.write_bytes(command)
    print(command.hex(' ').upper())
