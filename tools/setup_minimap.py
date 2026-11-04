"""User-driven setup UI. Game Settings integration is a separate unfinished feature."""
import datetime, json, os, queue, subprocess, sys, threading, zipfile
from pathlib import Path
import tkinter as tk
from tkinter import ttk,filedialog,messagebox
import install_manager as installer
from import_map_registry import import_registry
from map_zones import read_zones
from startup_session import verify_install,worker_command

def data_root():return Path(os.environ['LOCALAPPDATA'])/'SnowRunnerMinimap'
def package_root():return Path(sys.executable).parent/'package' if getattr(sys,'frozen',False) else Path(__file__).resolve().parent.parent/'package'

def import_local_maps(game, directory):
    client=Path(game)/'preload/paks/client'
    path,registry=import_registry(client,directory)
    if not registry['maps']:raise ValueError('No supported map was imported. Read the coverage report in '+str(path))
    zones={'schema':'srmm-zone-catalog-v1','maps':{},'unavailable':[],'runtime_alignment_verified':False}
    for entry in registry['maps']:
        level=entry['map']
        try:zones['maps'][level]=read_zones(client/(level+'.pak'),level)
        except (OSError,ValueError,zipfile.BadZipFile) as e:zones['unavailable'].append({'map':level,'reason':str(e)})
    zone_path=Path(directory)/'zones.local.json';installer.atomic_json(zone_path,zones)
    registry=dict(registry,zone_support={'supported_maps':len(zones['maps']),'unavailable_maps':len(zones['unavailable'])})
    return path,zone_path if zones['maps'] else None,registry

def steam_choices():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\Valve\Steam') as key:
            root=winreg.QueryValueEx(key,'SteamPath')[0]
        return [str(p) for p in installer.steam_candidates(root)]
    except (OSError,ValueError):return []

def main():
    root=tk.Tk();root.title('SnowRunner Minimap Setup');root.resizable(False,False)
    data=data_root();data.mkdir(parents=True,exist_ok=True)
    prefs_path=data/'setup.local.json';prefs={}
    try:prefs=installer.read_json(prefs_path)
    except (OSError,ValueError):pass
    frame=ttk.Frame(root,padding=18);frame.grid()
    ttk.Label(frame,text='SnowRunner Minimap',font=('Segoe UI',16,'bold')).grid(row=0,column=0,columnspan=3,sticky='w')
    ttk.Label(frame,text='Experimental · one supported Windows Steam build\nMap images are imported from your installation and stay on this PC.').grid(row=1,column=0,columnspan=3,sticky='w',pady=8)
    game=tk.StringVar(value=prefs.get('game',''));save=tk.StringVar(value=prefs.get('save',''))
    choices=steam_choices()
    if not game.get() and len(choices)==1:game.set(choices[0])
    ttk.Label(frame,text='SnowRunner folder').grid(row=2,column=0,sticky='w')
    ttk.Combobox(frame,textvariable=game,values=choices,width=64).grid(row=2,column=1)
    def choose_game():
        p=filedialog.askdirectory(title='Choose SnowRunner folder (contains Sources and preload)')
        if p:game.set(p)
    ttk.Button(frame,text='Browse',command=choose_game).grid(row=2,column=2,padx=6)
    ttk.Label(frame,text='Saved destinations (optional)').grid(row=3,column=0,sticky='w')
    ttk.Entry(frame,textvariable=save,width=67).grid(row=3,column=1)
    def choose_save():
        p=filedialog.askopenfilename(title='Choose your exact CompleteSave[slot].cfg; blank disables saved destinations',filetypes=[('SnowRunner save','CompleteSave*.cfg')])
        if p:save.set(p)
    ttk.Button(frame,text='Browse',command=choose_save).grid(row=3,column=2,padx=6)
    record=tk.BooleanVar(value=False)
    ttk.Checkbutton(frame,text='Record local diagnostic positions for this session (optional)',variable=record).grid(row=4,column=0,columnspan=3,sticky='w',pady=8)
    status=tk.StringVar(value='Close SnowRunner for installation or removal. Start it using the button after importing maps.')
    ttk.Label(frame,textvariable=status,wraplength=750).grid(row=6,column=0,columnspan=3,sticky='w',pady=12)
    jobs=queue.Queue();busy=False;child=None;child_log=None;buttons=[]
    def work(task):
        nonlocal busy
        if busy:return
        busy=True
        for b in buttons:b.configure(state='disabled')
        def run():
            try:jobs.put((True,task()))
            except Exception as e:jobs.put((False,str(e)))
        threading.Thread(target=run,daemon=True).start()
    def save_prefs(values):
        prefs.update(values);installer.atomic_json(prefs_path,prefs)
    def install():
        selected=game.get()
        try:plan=installer.plan_install(selected,package_root(),data/'installation')
        except (OSError,ValueError) as e:messagebox.showerror('Cannot install',str(e));return
        if messagebox.askyesno('Install original minimap DLLs?',f"Target: {plan['target']}\n\nAdds dxgi.dll and d3d11.dll. Ownership records and rollback copies are kept locally. Saves are not changed."):
            work(lambda:('Installed. Next: import installed maps.',installer.install(selected,package_root(),data/'installation'))[0])
    def maps():
        selected=game.get()
        def run():
            verify_install(selected,package_root())
            directory=data/'maps'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
            path,zones,registry=import_local_maps(selected,directory)
            save_prefs({'game':selected,'registry':str(path),'zones':str(zones) if zones else ''})
            support=registry['zone_support']
            return (f"Imported {len(registry['maps'])}/{registry['coverage']['candidate_count']} candidate maps; {len(registry['unavailable'])} unavailable. "
                    f"Static destinations: {support['supported_maps']} maps supported, {support['unavailable_maps']} unavailable. "
                    f"Alignment is unverified. Report: {path}")
        status.set('Reading installed map archives. This can take a few minutes.');work(run)
    def start():
        nonlocal child,child_log
        if child and child.poll() is None:status.set('This setup already started a session. Use its session-control window.');return
        selected=game.get();saved=save.get()
        try:
            verify_install(selected,package_root())
            registry=Path(prefs.get('registry',''))
            if not registry.is_file() or prefs.get('game')!=selected:raise ValueError('Import installed maps for this folder first.')
            if saved:
                from destination_source import SavedWaypointSource
                SavedWaypointSource(saved)
                if not Path(saved).is_file():raise ValueError('Selected save does not exist.')
            save_prefs({'game':selected,'save':saved})
            args=['--game-root',selected,'--package',str(package_root()),'--registry',str(registry),'--launch-game']
            if saved:
                args+=['--save',saved]
                if prefs.get('zones'):args+=['--zones',prefs['zones']]
            if record.get():args+=['--record']
            log=data/('startup-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'.local.log')
            command=worker_command([sys.executable,str(Path(__file__).with_name('startup_session.py')),*args],log)
            with log.open('a',encoding='utf8') as output:
                child=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            child_log=log
            status.set('Starting Steam and a game-bound session. Startup log: '+str(log))
        except (OSError,ValueError) as e:messagebox.showerror('Cannot start',str(e))
    def uninstall():
        selected=game.get()
        try:plan=installer.plan_uninstall(selected,data/'installation')
        except (OSError,ValueError) as e:messagebox.showerror('Cannot remove',str(e));return
        if messagebox.askyesno('Remove minimap?',f"Remove only the unchanged DLLs owned by this installer from {plan['target']}?\nYour saves, maps and preferences will be retained."):
            work(lambda:('Removed owned DLLs. Local maps and preferences retained.',installer.uninstall(selected,data/'installation'))[0])
    def recover():
        selected=game.get()
        if messagebox.askyesno('Recover interrupted installation?', 'Close SnowRunner first. Recovery checks ownership and restores the previous recorded DLL state. Changed or unknown files will be left alone.'):
            work(lambda:str(installer.recover(selected,data/'installation')))
    def migrate():
        selected=game.get()
        try:plan=installer.plan_adopt_legacy(selected,data/'installation')
        except (OSError,ValueError) as e:messagebox.showerror('Cannot migrate development build',str(e));return
        if messagebox.askyesno('Adopt known development build?',f"Target: {plan['target']}\nBuild: {plan['version']}\n\nThis exact original DLL pair will be backed up and recorded as installer-owned. DLLs stay unchanged during migration. Install / repair can then upgrade it, and Uninstall can remove it. Close SnowRunner before proceeding."):
            work(lambda:('Development build backed up and adopted. Next: Install / repair.',installer.adopt_legacy(selected,data/'installation'))[0])
    def poll():
        nonlocal busy,child
        try:
            ok,text=jobs.get_nowait();busy=False;status.set(text)
            for b in buttons:b.configure(state='normal')
            if not ok:messagebox.showerror('Operation stopped',text)
        except queue.Empty:pass
        if child and child.poll() is not None:
            status.set('Session controller closed.' if child.returncode==0 else 'Startup failed. Local log: '+str(child_log))
            if child.returncode and child_log:
                try:
                    with child_log.open('rb') as source:
                        source.seek(0,2);size=source.tell();source.seek(max(0,size-3000));detail=source.read().decode('utf8',errors='replace')
                except OSError:detail='The log could not be read.'
                messagebox.showerror('Startup stopped',detail+'\n\nLog: '+str(child_log))
            child=None
        root.after(300,poll)
    bar=ttk.Frame(frame);bar.grid(row=5,column=0,columnspan=3,sticky='w')
    for label,callback in [('1. Install / repair',install),('2. Import maps',maps),('3. Start game + minimap',start),('Uninstall',uninstall),('Recover',recover)]:
        b=ttk.Button(bar,text=label,command=callback);b.pack(side='left',padx=3);buttons.append(b)
    legacy=ttk.Button(frame,text='Migrate known development build',command=migrate)
    legacy.grid(row=7,column=0,columnspan=3,sticky='w');buttons.append(legacy)
    def close():
        if busy:messagebox.showinfo('Operation running','Wait for the current file operation to finish before closing.');return
        root.destroy()
    root.protocol('WM_DELETE_WINDOW',close);root.after(300,poll);root.mainloop()

if __name__=='__main__':main()
