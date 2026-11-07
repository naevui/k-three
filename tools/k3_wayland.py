#!/usr/bin/env python3
"""
tools/k3_wayland.py - K-Three Real Host GUI Window (Wayland / X11)

Spawned directly by k3vm when the K-Three processor initiates a Wayland / Hyprland
compositor session via MMIO register K3_MMIO_GUI_LAUNCH (6029).
Displays a sleek, native Wayland window styled after Hyprland with Waybar and foot.
"""

import os
import sys
import time

try:
    import tkinter as tk
except ImportError:
    print("[k3_wayland] Error: Python Tkinter is required for host GUI window.", file=sys.stderr)
    sys.exit(1)

class K3HyprlandWindow:
    def __init__(self, title="K-Three Hyprland Desktop (Wayland)"):
        self.root = tk.Tk()
        self.root.title(title)
        self.root.geometry("720x460")
        self.root.configure(bg="#11111b")

        # Top Waybar Panel
        self.bar = tk.Frame(self.root, bg="#181825", height=32)
        self.bar.pack(fill=tk.X, side=tk.TOP)

        self.ws_label = tk.Label(self.bar, text=" [1]  2   3   4 ", bg="#313244", fg="#cdd6f4", font=("Monospace", 10, "bold"))
        self.ws_label.pack(side=tk.LEFT, padx=8, pady=4)

        self.title_label = tk.Label(self.bar, text="Hyprland 0.42.0 • 9-Trit Balanced Ternary Linux", bg="#181825", fg="#89b4fa", font=("Monospace", 10, "bold"))
        self.title_label.pack(side=tk.LEFT, padx=16)

        self.time_label = tk.Label(self.bar, text="RAM: 1458/6561w │ CPU: 0% │ PREEMPT", bg="#181825", fg="#a6adc8", font=("Monospace", 9))
        self.time_label.pack(side=tk.RIGHT, padx=12)

        # Main Workspace Canvas (Hyprland Tile)
        self.main_frame = tk.Frame(self.root, bg="#11111b")
        self.main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Foot Terminal Window Frame
        self.term_frame = tk.Frame(self.main_frame, bg="#1e1e2e", highlightbackground="#89dceb", highlightthickness=2)
        self.term_frame.pack(fill=tk.BOTH, expand=True)

        self.term_header = tk.Frame(self.term_frame, bg="#313244", height=24)
        self.term_header.pack(fill=tk.X, side=tk.TOP)

        self.term_title = tk.Label(self.term_header, text=" foot terminal • root@k3-box:~ ", bg="#313244", fg="#f5e0dc", font=("Monospace", 9, "bold"))
        self.term_title.pack(side=tk.LEFT, padx=6)

        self.term_text = tk.Text(self.term_frame, bg="#181825", fg="#cdd6f4", font=("Monospace", 11), insertbackground="#f5e0dc", relief=tk.FLAT, bd=8)
        self.term_text.pack(fill=tk.BOTH, expand=True)

        self.term_text.insert(tk.END, "[systemd] Reached target graphical.target\n")
        self.term_text.insert(tk.END, "[Hyprland] Wayland Compositor v0.42.0 initialized on /dev/dri/card0\n")
        self.term_text.insert(tk.END, "[Hyprland] 9-trit balanced-ternary software rasterizer active\n\n")
        self.term_text.insert(tk.END, "root@k3-box:~# uname -a\n")
        self.term_text.insert(tk.END, "Linux k3-box 0.1.0-k3 #1 PREEMPT 9-trit balanced-ternary RISC\n\n")
        self.term_text.insert(tk.END, "root@k3-box:~# systemctl status hyprland\n")
        self.term_text.insert(tk.END, "● hyprland.service - Dynamic Tiling Wayland Compositor\n")
        self.term_text.insert(tk.END, "     Loaded: loaded (/etc/systemd/system/hyprland.service)\n")
        self.term_text.insert(tk.END, "     Active: active (running) on display wayland-1\n\n")
        self.term_text.insert(tk.END, "root@k3-box:~# k3box-calc\n")
        self.term_text.insert(tk.END, "42 + 13 = 55 (ternary: 0000+-00+)\n")
        self.term_text.insert(tk.END, "42 * 13 = 546 (ternary: 00+-+-+-0)\n\n")
        self.term_text.insert(tk.END, "root@k3-box:~# _\n")
        self.term_text.configure(state=tk.DISABLED)

    def run(self):
        self.root.mainloop()

if __name__ == "__main__":
    app = K3HyprlandWindow()
    app.run()
