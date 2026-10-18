#!/usr/bin/env python3
"""
flasher_gui.py - ArvaNode Graphical ESP32 Flasher & Profile Manager
ArvaCore Smart Irrigation Ecosystem (2026)

Refactored Architecture:
- Uses canonical NodeConfig and modular services (arvanode_core).
- Supports flashing directly from in-memory configuration without requiring saved profiles.
- Profiles act as optional persistent presets.
- Clean subprocess isolation prevents "The instance is no longer valid" stale daemon bugs.
"""

import os
import sys
import threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from arvanode_core import (
    NodeConfig,
    NetworkConfig,
    SensorConfig,
    PinoutConfig,
    ProfileRepository,
    BuildService,
    FlashService,
    ProvisioningOrchestrator
)


class ArvaNodeFlasherApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ArvaNode — ESP32 Flasher & Configuration Tool")
        self.geometry("880x790")
        self.minsize(800, 720)

        # Initialize modular services
        self.repo = ProfileRepository()
        self.builder = BuildService(repo_root=REPO_ROOT)
        self.flash_svc = FlashService()
        self.orchestrator = ProvisioningOrchestrator(
            repository=self.repo,
            build_service=self.builder,
            flash_service=self.flash_svc
        )

        self._configure_styles()
        self._build_ui()
        self.refresh_profiles()
        self.refresh_ports()

    def _configure_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")

        self.bg_color = "#f4f5f7"
        self.primary_color = "#1976d2"
        self.configure(bg=self.bg_color)

        style.configure(".", background=self.bg_color, font=("Sans", 10))
        style.configure("TLabel", background=self.bg_color)
        style.configure("TLabelframe", background=self.bg_color)
        style.configure("TLabelframe.Label", font=("Sans", 10, "bold"), foreground="#333333")
        style.configure("Header.TLabel", font=("Sans", 15, "bold"), foreground="#1565c0")
        style.configure("SubHeader.TLabel", font=("Sans", 9), foreground="#666666")
        style.configure("Primary.TButton", font=("Sans", 10, "bold"), foreground="white", background=self.primary_color)
        style.map("Primary.TButton", background=[("active", "#1565c0")])

    def _build_ui(self):
        header = ttk.Frame(self, padding=(16, 12, 16, 6))
        header.pack(fill=tk.X)
        ttk.Label(header, text="🌱 ArvaNode Firmware Flasher & Configuration Tool", style="Header.TLabel").pack(anchor=tk.W)
        ttk.Label(
            header,
            text="Modular ESP32 flasher: customize WiFi, API Gateway, and Calibration settings, then compile & flash.",
            style="SubHeader.TLabel"
        ).pack(anchor=tk.W)

        content = ttk.Frame(self, padding=(16, 6, 16, 6))
        content.pack(fill=tk.BOTH, expand=True)

        # Top Bar: Profile Preset & Serial Port
        top_bar = ttk.LabelFrame(content, text="1. Target Device & Preset Selection", padding=10)
        top_bar.pack(fill=tk.X, pady=(0, 8))

        ttk.Label(top_bar, text="Load Preset:").grid(row=0, column=0, sticky=tk.W, padx=4, pady=4)
        self.profile_combo = ttk.Combobox(top_bar, state="readonly", width=22)
        self.profile_combo.grid(row=0, column=1, sticky=tk.W, padx=4, pady=4)
        self.profile_combo.bind("<<ComboboxSelected>>", self.on_profile_selected)

        ttk.Button(top_bar, text="Save As New Preset", command=self.create_new_profile_dialog).grid(row=0, column=2, padx=4, pady=4)

        ttk.Label(top_bar, text="Serial Port:").grid(row=0, column=3, sticky=tk.W, padx=(16, 4), pady=4)
        self.port_combo = ttk.Combobox(top_bar, state="readonly", width=18)
        self.port_combo.grid(row=0, column=4, sticky=tk.W, padx=4, pady=4)

        ttk.Button(top_bar, text="🔄 Refresh Ports", command=self.refresh_ports).grid(row=0, column=5, padx=4, pady=4)

        # Middle Section: In-Memory Configuration Form
        cfg_frame = ttk.LabelFrame(content, text="2. Active Configuration (Edit values directly)", padding=10)
        cfg_frame.pack(fill=tk.X, pady=(0, 8))

        # Row 0: Node ID & Display Name
        ttk.Label(cfg_frame, text="Node ID:").grid(row=0, column=0, sticky=tk.W, padx=4, pady=3)
        self.var_node_id = tk.StringVar(value="ARVANODE-001")
        self.entry_node_id = ttk.Entry(cfg_frame, textvariable=self.var_node_id, width=20)
        self.entry_node_id.grid(row=0, column=1, sticky=tk.W, padx=4, pady=3)
        self.var_node_id.trace_add("write", self._on_node_id_change)

        ttk.Label(cfg_frame, text="Display Name:").grid(row=0, column=2, sticky=tk.W, padx=(16, 4), pady=3)
        self.var_display_name = tk.StringVar(value="ArvaNode Field 01")
        ttk.Entry(cfg_frame, textvariable=self.var_display_name, width=32).grid(row=0, column=3, sticky=tk.W, padx=4, pady=3)

        # Row 1: WiFi SSID & Password
        ttk.Label(cfg_frame, text="WiFi SSID:").grid(row=1, column=0, sticky=tk.W, padx=4, pady=3)
        self.var_wifi_ssid = tk.StringVar(value="Cream")
        ttk.Entry(cfg_frame, textvariable=self.var_wifi_ssid, width=20).grid(row=1, column=1, sticky=tk.W, padx=4, pady=3)

        ttk.Label(cfg_frame, text="WiFi Password:").grid(row=1, column=2, sticky=tk.W, padx=(16, 4), pady=3)
        self.var_wifi_pass = tk.StringVar(value="hatdog123")
        self.entry_wifi_pass = ttk.Entry(cfg_frame, textvariable=self.var_wifi_pass, show="*", width=32)
        self.entry_wifi_pass.grid(row=1, column=3, sticky=tk.W, padx=4, pady=3)

        self.var_show_pass = tk.BooleanVar(value=False)
        ttk.Checkbutton(cfg_frame, text="Show", variable=self.var_show_pass, command=self.toggle_show_pass).grid(row=1, column=4, sticky=tk.W, padx=2)

        # Row 2: Backend URL
        ttk.Label(cfg_frame, text="Backend URL:").grid(row=2, column=0, sticky=tk.W, padx=4, pady=3)
        self.var_backend_url = tk.StringVar(value="https://api-aquaflow.gladwindr.me/api/v1")
        ttk.Entry(cfg_frame, textvariable=self.var_backend_url, width=65).grid(row=2, column=1, columnspan=4, sticky=tk.W, padx=4, pady=3)

        # Row 3: Gateway Key
        ttk.Label(cfg_frame, text="Gateway Key:").grid(row=3, column=0, sticky=tk.W, padx=4, pady=3)
        self.var_gateway_key = tk.StringVar(value="ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9")
        ttk.Entry(cfg_frame, textvariable=self.var_gateway_key, width=65).grid(row=3, column=1, columnspan=4, sticky=tk.W, padx=4, pady=3)

        # Row 4: Sensor & Calibration parameters
        ttk.Label(cfg_frame, text="Datum Offset (cm):").grid(row=4, column=0, sticky=tk.W, padx=4, pady=3)
        self.var_datum = tk.StringVar(value="20.0")
        ttk.Entry(cfg_frame, textvariable=self.var_datum, width=12).grid(row=4, column=1, sticky=tk.W, padx=4, pady=3)

        ttk.Label(cfg_frame, text="Soil Air ADC / Water ADC:").grid(row=4, column=2, sticky=tk.W, padx=(16, 4), pady=3)
        cal_subframe = ttk.Frame(cfg_frame)
        cal_subframe.grid(row=4, column=3, sticky=tk.W, padx=4, pady=3)
        self.var_soil_air = tk.StringVar(value="3200")
        self.var_soil_water = tk.StringVar(value="1500")
        ttk.Entry(cal_subframe, textvariable=self.var_soil_air, width=7).pack(side=tk.LEFT)
        ttk.Label(cal_subframe, text=" / ").pack(side=tk.LEFT)
        ttk.Entry(cal_subframe, textvariable=self.var_soil_water, width=7).pack(side=tk.LEFT)

        # Preset Save Actions
        cfg_btn_frame = ttk.Frame(cfg_frame)
        cfg_btn_frame.grid(row=5, column=0, columnspan=5, sticky=tk.E, pady=(8, 2))
        ttk.Button(cfg_btn_frame, text="💾 Update Current Preset", command=self.save_current_preset).pack(side=tk.RIGHT, padx=4)
        ttk.Button(cfg_btn_frame, text="📝 Export node_config.h", command=self.export_header_only).pack(side=tk.RIGHT, padx=4)

        # Action Controls
        act_frame = ttk.LabelFrame(content, text="3. Compile & Flash Operations", padding=10)
        act_frame.pack(fill=tk.X, pady=(0, 8))

        self.var_dry_run = tk.BooleanVar(value=False)
        ttk.Checkbutton(act_frame, text="Dry-run simulation mode (test commands without hardware)", variable=self.var_dry_run).pack(side=tk.LEFT, padx=6)

        btn_container = ttk.Frame(act_frame)
        btn_container.pack(side=tk.RIGHT)

        ttk.Button(btn_container, text="🔨 Compile Only", command=lambda: self.run_operation("compile")).pack(side=tk.LEFT, padx=4)
        self.btn_flash = ttk.Button(btn_container, text="⚡ Flash ARVANODE-001", style="Primary.TButton", command=lambda: self.run_operation("flash"))
        self.btn_flash.pack(side=tk.LEFT, padx=4)

        # Console & Log Output
        log_frame = ttk.LabelFrame(content, text="Operation Log & Diagnostics", padding=8)
        log_frame.pack(fill=tk.BOTH, expand=True)

        self.log_text = scrolledtext.ScrolledText(log_frame, wrap=tk.WORD, font=("Monospace", 9), bg="#1e1e1e", fg="#d4d4d4", insertbackground="white")
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.log("Ready. Configure your network and sensor parameters above, then click '⚡ Flash'.")

    def _on_node_id_change(self, *args):
        val = self.var_node_id.get().strip() or "Custom"
        self.btn_flash.config(text=f"⚡ Flash {val}")

    def toggle_show_pass(self):
        self.entry_wifi_pass.config(show="" if self.var_show_pass.get() else "*")

    def log(self, message):
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)

    def refresh_ports(self):
        devices = self.flash_svc.discover_serial_ports()
        port_list = [d["port"] for d in devices]
        self.port_combo["values"] = port_list
        if port_list:
            self.port_combo.current(0)
            self.log(f"Detected {len(port_list)} serial port(s): {', '.join(port_list)}")
        else:
            self.port_combo.set("")
            self.log("No active serial devices detected on /dev/ttyUSB* or /dev/ttyACM*.")

    def refresh_profiles(self):
        self.repo.ensure_default_profiles()
        profiles = self.repo.list_profiles()
        ids = [p.logical_node_id for p in profiles]
        self.profile_combo["values"] = ids
        if ids:
            self.profile_combo.current(0)
            self.load_preset(ids[0])

    def on_profile_selected(self, event):
        selected_id = self.profile_combo.get()
        if selected_id:
            self.load_preset(selected_id)

    def load_preset(self, node_id):
        cfg = self.repo.load_profile(node_id)
        if not cfg:
            return

        self.var_node_id.set(cfg.logical_node_id)
        self.var_display_name.set(cfg.display_name)
        self.var_wifi_ssid.set(cfg.network.wifi_ssid)
        self.var_wifi_pass.set(cfg.network.wifi_password)
        self.var_backend_url.set(cfg.network.backend_base_url)
        self.var_gateway_key.set(cfg.network.gateway_key)
        self.var_datum.set(str(cfg.sensor.datum_offset_cm))
        self.var_soil_air.set(str(cfg.sensor.soil_air_value))
        self.var_soil_water.set(str(cfg.sensor.soil_water_value))

        self.btn_flash.config(text=f"⚡ Flash {cfg.logical_node_id}")
        self.log(f"Loaded preset: {node_id} ({cfg.display_name})")

    def get_current_config(self) -> NodeConfig:
        """Constructs an in-memory NodeConfig object directly from current form fields."""
        network = NetworkConfig(
            wifi_ssid=self.var_wifi_ssid.get().strip(),
            wifi_password=self.var_wifi_pass.get().strip(),
            backend_base_url=self.var_backend_url.get().strip(),
            gateway_key=self.var_gateway_key.get().strip()
        )

        sensor = SensorConfig(
            datum_offset_cm=float(self.var_datum.get().strip() or 20.0),
            soil_air_value=int(self.var_soil_air.get().strip() or 3200),
            soil_water_value=int(self.var_soil_water.get().strip() or 1500)
        )

        return NodeConfig(
            logical_node_id=self.var_node_id.get().strip() or "ARVANODE-001",
            display_name=self.var_display_name.get().strip() or "ArvaNode",
            network=network,
            sensor=sensor
        )

    def save_current_preset(self):
        try:
            cfg = self.get_current_config()
            saved_path = self.repo.save_profile(cfg)
            self.log(f"Preset successfully updated: {saved_path}")
            messagebox.showinfo("Preset Saved", f"Profile preset '{cfg.logical_node_id}' saved successfully.")
        except Exception as e:
            messagebox.showerror("Validation Error", str(e))

    def export_header_only(self):
        try:
            cfg = self.get_current_config()
            header_path = self.builder.generate_header(cfg)
            self.log(f"Header generated: {header_path}")
            messagebox.showinfo("Header Exported", f"Build configuration header exported to:\n{header_path}")
        except Exception as e:
            messagebox.showerror("Export Error", str(e))

    def create_new_profile_dialog(self):
        dialog = tk.Toplevel(self)
        dialog.title("Save As New Profile Preset")
        dialog.geometry("380x180")
        dialog.transient(self)
        dialog.grab_set()

        ttk.Label(dialog, text="Logical Node ID (e.g. ARVANODE-011):").pack(anchor=tk.W, padx=16, pady=(16, 4))
        entry_id = ttk.Entry(dialog, width=30)
        entry_id.pack(padx=16, pady=4)
        entry_id.focus()

        ttk.Label(dialog, text="Display Name:").pack(anchor=tk.W, padx=16, pady=(8, 4))
        entry_name = ttk.Entry(dialog, width=30)
        entry_name.pack(padx=16, pady=4)

        def on_confirm():
            new_id = entry_id.get().strip().upper()
            new_name = entry_name.get().strip() or None
            if not new_id:
                messagebox.showwarning("Warning", "Node ID cannot be empty.")
                return
            try:
                self.repo.create_default_profile(new_id, new_name)
                dialog.destroy()
                self.refresh_profiles()
                self.profile_combo.set(new_id)
                self.load_preset(new_id)
                messagebox.showinfo("Created", f"Profile preset '{new_id}' generated successfully.")
            except Exception as e:
                messagebox.showerror("Error", str(e))

        btn_row = ttk.Frame(dialog)
        btn_row.pack(fill=tk.X, padx=16, pady=16)
        ttk.Button(btn_row, text="Cancel", command=dialog.destroy).pack(side=tk.RIGHT, padx=4)
        ttk.Button(btn_row, text="Create", style="Primary.TButton", command=on_confirm).pack(side=tk.RIGHT, padx=4)

    def run_operation(self, op_type):
        try:
            config = self.get_current_config()
            config.validate()
        except ValueError as err:
            messagebox.showerror("Configuration Error", str(err))
            return

        port = self.port_combo.get().strip() or None
        dry_run = self.var_dry_run.get()

        if op_type == "flash" and not port and not dry_run:
            messagebox.showerror("Serial Port Error", "No serial port selected. Connect your ESP32 or enable Dry-run mode.")
            return

        self.btn_flash.config(state="disabled")

        def worker():
            try:
                self.log("\n" + "=" * 65)
                if op_type == "compile":
                    self.log(f"Starting Compilation for '{config.logical_node_id}' (Dry-run: {dry_run})...")
                    res = self.orchestrator.run_compile_only(config, dry_run=dry_run)
                    if res.success:
                        self.log("✅ Firmware compiled cleanly without errors.")
                        if res.build_result and res.build_result.output:
                            self.log(res.build_result.output.strip())
                    else:
                        self.log(f"❌ {res.message}")
                elif op_type == "flash":
                    self.log(f"Starting Flash for '{config.logical_node_id}' on {port} (Dry-run: {dry_run})...")
                    res = self.orchestrator.run_full_provision(config, port=port, dry_run=dry_run)
                    if res.success:
                        self.log("🎉 ESP32 Firmware Upload Completed Successfully!")
                        if res.flash_result and res.flash_result.output:
                            self.log(res.flash_result.output.strip())
                    else:
                        self.log(f"❌ Operation failed at stage [{res.stage}]: {res.message}")
            except Exception as ex:
                self.log(f"❌ Unexpected error during execution: {ex}")
            finally:
                self.btn_flash.config(state="normal")

        threading.Thread(target=worker, daemon=True).start()


def main():
    app = ArvaNodeFlasherApp()
    app.mainloop()


if __name__ == "__main__":
    main()
