"""Disposable extraction diagnostics, separate from the extracted source assets."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]


def report_path(name):
    target=ROOT/'gba/build-generated/reports/extraction'/name
    target.parent.mkdir(parents=True,exist_ok=True)
    return target
