#!/usr/bin/env python3
"""Windows-обёртка над run.py: остановка сервера без SIGINT.

subprocess.send_signal(SIGINT) на Windows поднимает CTRL_C_EVENT на всю
группу процессов — падает сам питон. Для стенда достаточно terminate().
Запуск: python run_win.py <те же аргументы, что у run.py>
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run as stand


def stop_win(self):
    if self.proc and self.proc.poll() is None:
        self.proc.terminate()
        try:
            self.proc.wait(timeout=15)
        except Exception:
            self.proc.kill()
            self.proc.wait(timeout=10)
    if self.log:
        self.log.close()
    stand.wait_port_free(self.port, timeout=60)


stand.Server.stop = stop_win

if __name__ == "__main__":
    stand.main()
