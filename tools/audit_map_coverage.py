"""Read-only installed-map coverage report. Does not extract or install assets."""
import argparse
import json
from pathlib import Path
from map_catalog import discover_installed


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path,help='New private JSON report outside the installation.')
    args=parser.parse_args()
    root=args.client_dir.resolve(strict=True)
    report=discover_installed(root)
    if args.output:
        output=args.output.resolve()
        if len(root.parents)<3 or output.is_relative_to(root.parents[2]):
            parser.error('Report must remain outside the game installation.')
        with output.open('x',encoding='utf8') as stream:json.dump(report,stream,indent=2)
    print(f"Found {report['ready_count']}/{report['candidate_count']} installed candidate textures; "
          f"{len(report['excluded'])} main-menu scenes excluded. Geometry and live alignment are separate checks.")
    for item in report['maps']:
        if item['status']!='ready_for_import':print(f"{item['map']}: {item['status']}: {item['reason']}")
    for item in report['archive_errors']:print(f"Archive error: {item['archive']}: {item['reason']}")
    if not report['inventory_complete'] or report['ready_count']!=report['candidate_count']:
        parser.exit(1,'Installed map coverage is incomplete; review the listed reasons.\n')


if __name__=='__main__':main()
