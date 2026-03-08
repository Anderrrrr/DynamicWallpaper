import ctypes
import os
import time

def add_task():
    # schtasks /create /tn "DynamicWallpaperBoot" /tr "cmd.exe" /sc onlogon /f
    args = '/create /tn "DynamicWallpaperBootTest" /tr "cmd.exe /c echo hello" /sc onlogon /f'
    ret = ctypes.windll.shell32.ShellExecuteW(None, "runas", "schtasks", args, None, 0)
    print("ShellExecute returned:", ret)

if __name__ == "__main__":
    add_task()
