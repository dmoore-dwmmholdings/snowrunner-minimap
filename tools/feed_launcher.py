"""User-launched control window for an explicit private minimap test manifest.

Launch normally from Windows, not inside a short-lived automation command. This
does not install anything, register services/tasks, inject, or discover processes.
"""
import argparse
import ctypes
import datetime
import os
from pathlib import Path
import re
import subprocess
import sys
import tkinter as tk
from tkinter import ttk, messagebox

from feed_lifecycle import MAX_SESSION_SECONDS, read_json, request_stop


def load_manifest(path):
    config=read_json(path)
    allowed={'schema','pid','session','map','layout','output_dir','config','registry','save','zone_catalog','saved_waypoints_preview','record','seconds'}
    if config.get('schema')!='srmm-launch-v1' or set(config)-allowed:raise ValueError('Invalid launch manifest schema/keys.')
    if type(config.get('pid')) is not int or not 0<config['pid']<2**32:raise ValueError('Explicit game PID required.')
    if type(config.get('session')) is not int or not 0<config['session']<2**64:raise ValueError('Explicit session required.')
    if not re.fullmatch(r'level_[a-z0-9_]{3,48}',config.get('map','')):raise ValueError('Canonical initial map required.')
    for key in ('layout','output_dir','config','registry','save','zone_catalog'):
        if key not in config:continue
        value=config[key]
        if not isinstance(value,str) or not Path(value).is_absolute():raise ValueError(f'{key} must be an absolute local path.')
        if key!='output_dir' and not Path(value).is_file():raise ValueError(f'{key} file is missing.')
    if 'layout' not in config or 'output_dir' not in config:raise ValueError('Layout and output directory required.')
    if ('config' in config)!=('registry' in config):raise ValueError('Map switching requires both config and registry.')
    for key in ('record','saved_waypoints_preview'):
        if key in config and type(config[key]) is not bool:raise ValueError(f'{key} must be boolean.')
    if config.get('save') and not config.get('saved_waypoints_preview'):raise ValueError('Saved waypoints require explicit preview selection.')
    if 'zone_catalog' in config and ('save' not in config or not config.get('saved_waypoints_preview')):raise ValueError('Zone catalog requires an explicit save and saved-destination preview.')
    seconds=config.get('seconds',MAX_SESSION_SECONDS)
    if type(seconds) is not int or not 1<=seconds<=MAX_SESSION_SECONDS:raise ValueError('Invalid bounded session duration.')
    config['seconds']=seconds
    return config


def feed_command(config,tools,python,record_path=None):
    command=[python,str(tools/'candidate_feed.py'),'--pid',str(config['pid']),
             '--session',str(config['session']),'--map',config['map'],'--layout',config['layout'],
             '--output-dir',config['output_dir'],'--until-game-exit','--seconds',str(config['seconds']),
             '--allow-map-change']
    output=Path(config['output_dir'])
    if output.exists():
        if not (output/'session.local.json').is_file() or not (output/'candidate.local.bin').is_file():
            raise ValueError('Existing output directory is not a resumable session.')
        command.append('--resume')
    if record_path is not None:command.extend(('--record',str(record_path)))
    return command


def companion_commands(config,tools,python):
    status=str(Path(config['output_dir'])/'status.local.json');commands=[]
    if 'registry' in config:
        commands.append(('Map selection',[python,str(tools/'map_switcher.py'),'--config',config['config'],
            '--registry',config['registry'],'--pid',str(config['pid']),'--seconds',str(config['seconds']),'--feed-status',status]))
    if 'save' in config:
        commands.append(('Saved destinations (alignment preview)',[python,str(tools/'destination_source.py'),
            '--save',config['save'],'--feed',config['output_dir'],'--pid',str(config['pid']),
            '--session',str(config['session']),'--seconds',str(config['seconds']),
            '--allow-saved-preview','--feed-status',status]))
    if 'zone_catalog' in config and 'save' in config:commands[-1][1].extend(('--zone-catalog',config['zone_catalog']))
    return commands


def matching_status(status,config,now):
    return (status.get('schema')=='srmm-feed-status-v1' and status.get('pid')==config['pid'] and
            status.get('session')==config['session'] and isinstance(status.get('run'),str) and
            re.fullmatch('[a-f0-9]{32}',status['run']) is not None and
            type(status.get('tick_ms')) is int and 0<=now-status['tick_ms']<=2000)


def owned_processes_finished(worker,companions):
    """A new run must not race the previous run's companion cleanup."""
    return (worker is None or worker.poll() is not None) and all(process.poll() is not None for _,process in companions)


def owned_status(status,config,worker,run,now):
    """Implicit window-close stops only the feed this window launched."""
    return (worker is not None and matching_status(status,config,now) and
            status.get('publisher_pid')==worker.pid and (run is None or status.get('run')==run))


def launch_companions(commands,launch,companions):
    # Append each successful launch immediately. A later failure must not lose
    # process ownership or permit a new run before that helper has stopped.
    for label,command in commands:
        process=launch(command,'companion')
        companions.append((label,process))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path)
    parser.add_argument('--start',action='store_true',help='Start this explicitly selected session when the window opens.')
    args=parser.parse_args();config=load_manifest(args.manifest)
    tools=Path(__file__).resolve().parent;output=Path(config['output_dir'])
    clock=ctypes.WinDLL('kernel32').GetTickCount64;clock.restype=ctypes.c_uint64
    window=tk.Tk();window.title('SnowRunner Minimap — session control');window.resizable(False,False)
    frame=ttk.Frame(window,padding=18);frame.grid()
    ttk.Label(frame,text=f"SnowRunner process {config['pid']} · up to {config['seconds']/3600:g} hours").grid(row=0,column=0,columnspan=2,sticky='w')
    ttk.Label(frame,text='This window controls the local feed. F10 opens settings inside the game.').grid(row=1,column=0,columnspan=2,sticky='w',pady=6)
    state=tk.StringVar(value='Ready. Start runs only the selected game/session.')
    detail=tk.StringVar(value='Closing this window requests a clean stop.')
    ttk.Label(frame,textvariable=state,wraplength=520).grid(row=2,column=0,columnspan=2,sticky='w',pady=8)
    ttk.Label(frame,textvariable=detail,wraplength=520).grid(row=3,column=0,columnspan=2,sticky='w',pady=6)
    worker=None;companions=[];run=None;started_children=False;closing=False;logs=[];launched_at=0;companion_error=None

    def launch(command,label):
        stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        log_path=args.manifest.resolve().parent/f'{label}-{stamp}.local.log'
        log=log_path.open('x',encoding='utf8');logs.append(log)
        return subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW,cwd=str(tools))

    def current():
        try:return read_json(output/'status.local.json')
        except (OSError,ValueError):return {}

    def start():
        nonlocal worker,run,started_children,companions,launched_at,companion_error
        try:
            if worker and worker.poll() is None:return
            if not owned_processes_finished(worker,companions):
                state.set('Previous helpers are still stopping; wait before resuming.')
                return
            observed=current()
            if matching_status(observed,config,clock()) and observed.get('state') in ('starting','running'):
                state.set('This session already has an active feed. Stop it before starting another.')
                return
            stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
            record=output/f'drive-{stamp}.local.jsonl' if config.get('record',True) else None
            command=feed_command(config,tools,sys.executable,record)
            worker=launch(command,'feed');run=None;started_children=False;companions=[];launched_at=clock();companion_error=None
            state.set('Starting and verifying the selected executable…')
        except (OSError,ValueError) as error:messagebox.showerror('Feed not started',str(error))

    def stop(owned_only=False):
        observed=current()
        if owned_only and not owned_status(observed,config,worker,run,clock()):return
        if matching_status(observed,config,clock()) and observed.get('state') in ('starting','running'):
            try:request_stop(output,observed);state.set('Stopping; the last position will be invalidated.')
            except (OSError,ValueError) as error:messagebox.showerror('Stop request failed',str(error))

    def close():
        nonlocal closing
        closing=True;stop(owned_only=True)
        if owned_processes_finished(worker,companions):window.destroy()

    def poll():
        nonlocal run,started_children,companions,companion_error
        observed=current();now=clock()
        # Closing during executable verification may precede the first heartbeat.
        # Send the stop once it becomes identifiable instead of waiting for hours.
        if closing and owned_status(observed,config,worker,run,now) and observed.get('state') in ('starting','running'):
            try:request_stop(output,observed)
            except (OSError,ValueError):pass
        if worker and worker.poll() is None:
            if matching_status(observed,config,now) and observed.get('publisher_pid')==worker.pid:
                if run is None:run=observed['run']
                if run!=observed['run']:state.set('Feed ownership changed; no replacement will be started.')
                elif observed.get('state')=='running':
                    state.set('Live feed running.' if observed.get('pose_available') else 'Feed running; truck pose currently unavailable.')
                    detail.set(f"Map: {observed.get('map','unknown')} · packet {observed.get('sequence',0)}" +
                               (f" · recording issue: {observed['record_error']}" if observed.get('record_error') else ''))
                    if not started_children and not closing:
                        started_children=True
                        try:launch_companions(companion_commands(config,tools,sys.executable),launch,companions)
                        except OSError as error:companion_error=f'Companion could not start: {error}'
                else:state.set(f"Feed: {observed.get('state','unknown')}")
            elif now-launched_at>5000:state.set('Feed heartbeat is stale. The in-game map will hide stale positions.')
        elif worker:
            reason=observed.get('state','no final status') if observed.get('publisher_pid')==worker.pid else 'startup failed; see local log'
            state.set(f"Feed stopped ({reason}); exit code {worker.returncode}.")
        for label,process in companions:
            if process.poll() is not None and (process.returncode!=0 or (worker and worker.poll() is None and not closing)):
                detail.set(f'{label} stopped with exit code {process.returncode}; see its local log.')
        if companion_error:detail.set(companion_error)
        if closing and owned_processes_finished(worker,companions):
            window.destroy();return
        window.after(250,poll)

    ttk.Button(frame,text='Start / resume',command=start).grid(row=4,column=0,sticky='w',pady=10)
    ttk.Button(frame,text='Stop feed',command=stop).grid(row=4,column=1,sticky='e',pady=10)
    window.protocol('WM_DELETE_WINDOW',close);window.after(100,poll)
    if args.start:window.after(150,start)
    window.mainloop()
    for log in logs:log.close()


if __name__=='__main__':main()
