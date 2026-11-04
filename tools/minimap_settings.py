"""Windows settings companion for an already configured local diagnostic session.

Uses Python's standard Tk widgets. Does not launch/attach to a game or install DLLs.
"""
import os
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox
from session_config import config_lock


def update_display(path, zoom, tracking, shape, left, top, size):
    with config_lock(path):
        return _update_display_locked(path,zoom,tracking,shape,left,top,size)

def _update_display_locked(path, zoom, tracking, shape, left, top, size):
    zoom = float(zoom)
    left, top, size = float(left), float(top), float(size)
    import math
    if not all(math.isfinite(v) for v in (zoom, left, top, size)):
        raise ValueError('Use finite numeric values.')
    if not .25 <= zoom <= 8 or min(left, top) < 0 or not 96 <= size <= 800:
        raise ValueError('Zoom: 0.25–8; size: 96–800; positions: zero or greater.')
    if tracking not in ('north', 'heading', 'motion') or shape not in ('circle', 'square'):
        raise ValueError('Invalid display choice.')
    data = path.read_text(encoding='utf-8')
    if len(data.encode('utf-8')) > 16384: raise ValueError('Session file is too large.')
    lines = [line.split('=', 1) for line in data.splitlines() if line]
    fields = dict(lines)
    if len(fields) != len(lines): raise ValueError('Duplicate session keys.')
    if fields.get('version') != '1' or fields.get('enabled') not in ('0','1'):
        raise ValueError('An enabled version 1 diagnostic session is required.')
    fields.update(zoom=str(zoom), tracking=tracking, shape=shape,
                  viewport=f'{left} {top} {size}')
    payload=''.join(f'{key}={value}\n' for key,value in fields.items())
    if len(payload.encode('utf-8'))>16384:raise ValueError('Updated session file is too large.')
    temporary = path.with_name('settings-next.local.ini')
    created = False
    try:
        with temporary.open('x', encoding='utf-8') as output:
            created = True
            output.write(payload)
        os.replace(temporary, path)
    finally:
        if created and temporary.exists(): temporary.unlink()


def main():
    path = Path(os.environ['LOCALAPPDATA'])/'SnowRunnerMinimap'/'session.local.ini'
    window = tk.Tk();window.title('SnowRunner Minimap — display settings')
    window.resizable(False, False)
    frame = ttk.Frame(window, padding=16);frame.grid()
    try:
        fields = dict(line.split('=', 1) for line in path.read_text(encoding='utf-8').splitlines() if line)
        left, top, size = fields['viewport'].split()
    except (OSError, KeyError, ValueError) as error:
        messagebox.showerror('No configured session', str(error));window.destroy();return
    values = [fields.get('zoom', '1'),fields.get('tracking', 'motion'),fields.get('shape', 'circle'),left,top,size]
    variables = [tk.StringVar(value=value) for value in values]
    for row,(label,var) in enumerate(zip(('Zoom','Tracking','Shape','Left (pixels)','Top (pixels)','Size (pixels)'),variables)):
        ttk.Label(frame,text=label).grid(row=row,column=0,sticky='w',padx=6,pady=5)
        if row in (1,2):
            widget=ttk.Combobox(frame,textvariable=var,state='readonly',values=('north','heading','motion') if row==1 else ('circle','square'))
        else: widget=ttk.Entry(frame,textvariable=var)
        widget.grid(row=row,column=1,padx=6,pady=5)
    status=tk.StringVar(value='F9 toggles visibility. Keep the viewport inside your screen.')
    def apply():
        try:
            update_display(path, *(var.get() for var in variables))
            status.set('Applied. Return to the game to see the changes.')
        except (OSError,ValueError) as error:messagebox.showerror('Settings not applied',str(error))
    ttk.Button(frame,text='Apply',command=apply).grid(row=6,column=1,sticky='e',pady=10)
    ttk.Label(frame,textvariable=status,wraplength=380).grid(row=7,column=0,columnspan=2)
    window.mainloop()


if __name__ == '__main__':main()
