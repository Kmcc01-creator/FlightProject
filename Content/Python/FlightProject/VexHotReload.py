"""VEX file watcher for automatic hot-reload in the editor."""

import os
import unreal
from pathlib import Path
from . import VexTools

class VexWatcher:
    def __init__(self):
        self.watch_dir = Path(unreal.Paths.project_dir()) / "Scripts" / "Vex"
        self.last_mtimes = {}
        self.handle = None
        
        if not self.watch_dir.exists():
            self.watch_dir.mkdir(parents=True, exist_ok=True)
            unreal.log(f"VexWatcher: Created watch directory {self.watch_dir}")

    def start(self):
        if self.handle:
            return
        
        unreal.log(f"VexWatcher: Starting watcher on {self.watch_dir}")
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        
        # Initial scan
        self.scan(recompile=False)

    def stop(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
            unreal.log("VexWatcher: Stopped")

    def scan(self, recompile=True):
        changed = []
        for vex_file in self.watch_dir.glob("*.vex"):
            mtime = os.path.getmtime(vex_file)
            last_mtime = self.last_mtimes.get(vex_file)
            
            if last_mtime is None or mtime > last_mtime:
                self.last_mtimes[vex_file] = mtime
                changed.append(vex_file)
        
        if recompile:
            for vex_file in changed:
                VexTools.recompile_file(str(vex_file))

    def tick(self, delta_time):
        # Throttle scan - only every ~1 second (using a simple frame counter or delta accumulation would be better)
        # For simplicity in this prototype, we'll scan every tick but glob is relatively fast for few files
        self.scan(recompile=True)

_instance = None

def start():
    global _instance
    if _instance is None:
        _instance = VexWatcher()
    _instance.start()

def stop():
    global _instance
    if _instance:
        _instance.stop()
