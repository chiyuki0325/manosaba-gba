#!/usr/bin/env python3
"""Extract the Japanese Naninovel scripts and Chinese localization documents."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from output_paths import report_path

import UnityPy


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GAME = ROOT / "original_game"
DEFAULT_OUT = ROOT / "original_scripts"
DUMP = Path(__file__).resolve().parent / "il2cpp_output" / "dump.cs"

STREAMING = Path("manosaba_Data/StreamingAssets/aa/StandaloneWindows64")
JA_GLOB = "naninovel-scripts_assets_naninovelscripts-*.bundle"
ZH_GLOB = "general-localization-zhhans-scripts-*.bundle"
GENERIC_DEFAULT_ALIASES = {"print", "if", "else", "while", "endIf", "endWhile", "goto", "gosub", "return", "stop"}
SCRIPT_CLASSES = {
    "CommandScriptLine", "CommentScriptLine", "EmptyScriptLine",
    "GenericTextScriptLine", "LabelScriptLine",
}
BASE_CLASSES = {"Command", "PrinterCommand", "ActorCommand"}


@dataclass
class VarDef:
    name: str
    type_name: str
    alias: str | None
    default: str | None


@dataclass
class CommandDef:
    name: str
    alias: str | None = None
    fields: list[VarDef] = field(default_factory=list)


def parse_dump(dump_path: Path) -> dict[str, CommandDef]:
    """Read Naninovel parameter aliases and defaults from IL2CPP dump.cs."""
    text = dump_path.read_text(encoding="utf-8", errors="replace")
    pattern = re.compile(
        r"(?P<attrs>(?:^\[[^\n]*\]\n)*)"
        r"^public (?:abstract )?class (?P<name>\w+)(?:<[^>]*>)?(?: : (?P<bases>[^\n]+))?\n\{",
        re.MULTILINE,
    )
    classes: dict[str, tuple[CommandDef, str]] = {}
    current_ns = None
    for match in pattern.finditer(text):
        preceding = text[max(0, match.start()-1000):match.start()]
        ns_matches = list(re.finditer(r"^// Namespace: (\w+(?:\.\w+)*)$", preceding, re.MULTILINE))
        if ns_matches:
            current_ns = ns_matches[-1].group(1)
        name = match.group("name")
        bases = (match.group("bases") or "").split(" // ", 1)[0]
        # Generic commands often inherit from another command whose name does not
        # end in "Command" (e.g. Gosub). Defer that decision until bases are known.
        end = text.find("\n}", match.end())
        if end < 0:
            continue
        body = text[match.end():end]
        attrs = match.group("attrs")
        alias_match = re.search(r'\[CommandAlias\("([^"]*)"\)\]', attrs)
        command = CommandDef(name, alias_match.group(1) if alias_match else None)
        for field_match in re.finditer(
            r"(?P<attrs>(?:\t\[[^\n]*\]\n)*)"
            r"\tpublic (?P<type>\w*Parameter) (?P<name>\w+);",
            body,
        ):
            field_attrs = field_match.group("attrs")
            alias = re.search(r'\[ParameterAlias\("([^"]*)"\)\]', field_attrs)
            default = re.search(r'\[ParameterDefaultValue\("([^"]*)"\)\]', field_attrs)
            command.fields.append(VarDef(
                field_match.group("name"), field_match.group("type"),
                alias.group(1) if alias else None,
                default.group(1) if default else None,
            ))
        qualified = f"{current_ns}.{name}" if current_ns else name
        classes[qualified] = (command, bases)

    def all_fields(cls: str, seen: set[str]) -> list[VarDef]:
        if cls in seen or cls not in classes:
            return []
        seen.add(cls)
        command, bases = classes[cls]
        inherited: list[VarDef] = []
        for base in re.findall(r"\b(\w+)\b", bases):
            candidates = [key for key in classes if key.split(".")[-1] == base]
            if len(candidates) == 1:
                inherited.extend(all_fields(candidates[0], seen))
            elif base in BASE_CLASSES:
                inherited.extend(all_fields("Naninovel." + base, seen))
        return command.fields + inherited

    class_names = {key.split(".")[-1] for key in classes}
    classes_without_empty_bodies = {
        cls for cls, (_, bases) in classes.items()
        if any(base in class_names or base in BASE_CLASSES
               for base in re.findall(r"\b\w+\b", bases))
        or classes[cls][0].fields
    }
    aliases: dict[str, CommandDef] = {}
    for cls, (command, _) in classes.items():
        aliases[cls] = CommandDef(command.name, command.alias or command.name,
                                  all_fields(cls, set()))
    for cls in classes_without_empty_bodies:
        command = aliases[cls]
        aliases.setdefault(command.alias, command)
    return aliases


def read_ja_scripts(streaming: Path) -> tuple[list[dict], dict[str, list[str]]]:
    result: list[dict] = []
    sources: dict[str, list[str]] = {}
    for bundle in sorted(streaming.glob(JA_GLOB)):
        for obj in UnityPy.load(str(bundle)).objects:
            if obj.type.name != "MonoBehaviour":
                continue
            tree = obj.read_typetree()
            if not all(k in tree for k in ("path", "lines", "textMap")):
                continue
            result.append(tree)
            sources.setdefault(tree["path"], []).append(bundle.name)
    return result, sources


def read_zh_scripts(streaming: Path) -> list[tuple[str, str, str]]:
    result: list[tuple[str, str, str]] = []
    for bundle in sorted(streaming.glob(ZH_GLOB)):
        for obj in UnityPy.load(str(bundle)).objects:
            if obj.type.name != "TextAsset":
                continue
            asset = obj.read()
            result.append((asset.m_Name, asset.m_Script, bundle.name))
    return result


def json_scalar(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return str(int(value)) if value.is_integer() else str(value)
    if value is None:
        return "null"
    return str(value)


def serialize_value(defn: VarDef, value: dict) -> str:
    if not value.get("hasValue"):
        return ""
    parts = value.get("raw", {}).get("parts", [])
    if parts:
        # Literal text is serialized as text; only runtime expressions get
        # braces. The braces are also the parameter's expression delimiter.
        return "".join(
            f"{{{part['expression']}}}" if part.get("expression")
            else part.get("text", "") for part in parts
        )
    v = value.get("value")
    name = defn.type_name
    if name.endswith("BooleanParameter"):
        return json_scalar(bool(v))
    if name.endswith("DecimalParameter"):
        return json_scalar(float(v))
    if name.endswith("IntegerParameter"):
        return json_scalar(int(v))
    if name == "LocalizableTextParameter" and isinstance(v, dict):
        return "".join(part.get("text", "") for part in v.get("parts", []))
    if name == "NamedStringParameter" and isinstance(v, dict):
        first = (v.get("name") or {}).get("value", "")
        second = (v.get("value") or {}).get("value", "")
        return f"{first}.{second}" if second else first
    if name in {"NamedStringListParameter", "DecimalListParameter"} and isinstance(v, list):
        return ",".join(
            json_scalar(item["value"]) if isinstance(item, dict)
            else str(item) for item in v
        )
    if isinstance(v, list):
        return ",".join(str(item) for item in v)
    return str(v if v is not None else "")


def quote(value: str) -> str:
    if not value:
        return ""
    if any(c in value for c in ' \t\n\r"{}[]\\'):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return value


def localizable_text(val: dict, text_map: dict[str, str],
                     allow_missing: bool = False) -> str:
    pieces = []
    for part in val.get("raw", {}).get("parts", []):
        key = part.get("id", "")
        text = part.get("text", "")
        if key:
            if key not in text_map:
                if not allow_missing:
                    raise ValueError(f"Missing Japanese text ID: {key}")
            else:
                text = text_map[key]
        pieces.append(f"{{{part['expression']}}}" if part.get("expression") else text)
    if pieces:
        return "".join(pieces)
    value = val.get("value")
    if isinstance(value, dict):
        return "".join(part.get("text", "") for part in value.get("parts", []))
    return str(value or "")


def format_command(entry: dict, command_defs: dict[str, CommandDef],
                   text_map: dict[str, str], full: bool = False) -> str:
    type_info = entry["type"]
    qualified = f"{type_info['ns']}.{type_info['class']}"
    command = command_defs.get(qualified) or command_defs.get(type_info["class"])
    if command is None:
        raise KeyError(f"Unknown command class: {type_info['class']}")
    ignored = {"ConditionalExpression", "playbackSpot", "indent"}
    defs = {f.name: f for f in command.fields if f.name not in ignored}
    data = entry["data"]
    # Some overridden commands keep an already-mapped localizable parameter in
    # their metadata instead of exposing it again as a public field.
    for key, val in data.items():
        if key not in defs and key not in ignored and isinstance(val, dict) and any(
                part.get("id") for part in val.get("raw", {}).get("parts", [])):
            defs[key] = VarDef(key, "LocalizableTextParameter", None, None)
    unknown = set(data) - set(defs) - ignored
    if unknown:
        raise KeyError(f"Unknown parameters in {type_info['class']}: {unknown}")
    tokens: list[str] = []
    params: list[str] = []
    for key, defn in defs.items():
        val = data.get(key)
        if not isinstance(val, dict) or not val.get("hasValue"):
            continue
        raw_parts = val.get("raw", {}).get("parts", [])
        # NamedStringParameter text is a literal path; it must not be looked up
        # as an inline localization ID.
        is_inline_text = (defn.name not in {"Path", "GotoPath"}
                          and any(part.get("id") for part in raw_parts))
        value = (localizable_text(val, text_map, allow_missing=True)
                 if is_inline_text else serialize_value(defn, val))
        if defn.alias == "":
            tokens.append(quote(value))
        elif ((full and (raw_parts or val.get("value") is not None))
              or (value != ""
                  and defn.default is not None and value != defn.default)
              # Expression parameters can have a falsy scalar default while
              # their raw value is a runtime expression; retain them.
              or any(part.get("expression") for part in raw_parts)):
            rendered = (value if any(part.get("expression") for part in raw_parts)
                        else quote(value))
            params.append(f"{defn.alias or defn.name}:{rendered}")
    if params:
        tokens.extend(["{"] + params + ["}"])
    return f"@{command.alias or type_info['class']} " + " ".join(tokens)


def render_script(tree: dict, command_defs: dict[str, CommandDef],
                  text_map: dict[str, str]) -> str:
    refs = {item["rid"]: item for item in tree["references"]["RefIds"]}
    output: list[str] = []
    for line_ref in tree["lines"]:
        line = refs[line_ref["rid"]]
        kind = line["type"]["class"]
        if kind not in SCRIPT_CLASSES:
            raise ValueError(f"Unknown script line class: {kind}")
        data = line["data"]
        prefix = "\t" * data.get("indent", 0)
        if kind == "EmptyScriptLine":
            output.append("")
        elif kind == "CommentScriptLine":
            comments = data.get("commentText", "").splitlines() or [""]
            output.extend(f"{prefix}# {text}" if text else f"{prefix}#"
                          for text in comments)
        elif kind == "LabelScriptLine":
            output.append(f"{prefix}# {data.get('labelText', '')}")
        elif kind == "CommandScriptLine":
            command = refs[data["command"]["rid"]]
            output.append(prefix + format_command(command, command_defs, text_map))
        elif kind == "GenericTextScriptLine":
            output.append(prefix + " ".join(
                format_command(refs[inline["rid"]], command_defs, text_map,
                               full=True)
                for inline in data.get("inlinedCommands", [])
            ))
    return "\n".join(output).rstrip() + "\n"


def script_relative(script_path: str) -> Path:
    relative = Path(script_path)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Unsafe script path: {script_path}")
    return relative.with_suffix(".nani")


def parse_zh(content: str) -> tuple[str, list[tuple[str, str]]]:
    first = content.splitlines()[0] if content else ""
    match = re.search(r"localization document for '([^']+)'", first)
    if not match:
        raise ValueError("Missing localization document script path")
    entries: list[tuple[str, list[str]]] = []
    for line in content.splitlines():
        if line.startswith("# "):
            entries.append((line[2:], []))
        elif line and not line.startswith(";") and entries:
            entries[-1][1].append(line)
    return match.group(1), [(key, "\n".join(lines)) for key, lines in entries]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--dump", type=Path, default=DUMP)
    args = parser.parse_args()
    streaming = args.game / STREAMING
    if not streaming.is_dir():
        parser.error(f"StreamingAssets not found: {streaming}")
    command_defs = parse_dump(args.dump)

    ja_scripts, sources = read_ja_scripts(streaming)
    if len(ja_scripts) != 440:
        raise ValueError(f"Expected 440 Japanese scripts, found {len(ja_scripts)}")
    ja_scripts.sort(key=lambda tree: tree["path"])
    paths = {tree["path"] for tree in ja_scripts}
    if len(paths) != len(ja_scripts):
        raise ValueError("Duplicate Japanese script paths")
    text_maps: dict[str, dict[str, str]] = {}
    for tree in ja_scripts:
        keys = tree["textMap"]["idToText"]["keys"]
        values = tree["textMap"]["idToText"]["values"]
        if len(keys) != len(values):
            raise ValueError(f"Malformed text map in {tree['path']}")
        text_maps[tree["path"]] = dict(zip(keys, values))

    for tree in ja_scripts:
        relative = script_relative(tree["path"])
        target = args.out / "ja" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(render_script(tree, command_defs, text_maps[tree["path"]]),
                          encoding="utf-8", newline="\n")

    zh_scripts: dict[str, tuple[str, list[tuple[str, str]], str]] = {}
    translated_entries = 0
    unmatched: list[dict] = []
    for name, content, source in read_zh_scripts(streaming):
        script_path, entries = parse_zh(content)
        if script_path in zh_scripts:
            raise ValueError(f"Duplicate Chinese script path: {script_path}")
        if script_path not in paths:
            raise ValueError(f"Chinese script missing in Japanese: {script_path}")
        unknown = [key for key, _ in entries if key not in text_maps[script_path]]
        if unknown:
            unmatched.append({"path": script_path, "count": len(unknown),
                              "keys": unknown[:5]})
        translated_entries += sum(bool(value.strip()) for _, value in entries)
        target = args.out / "zh-Hans" / script_relative(script_path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content.rstrip() + "\n", encoding="utf-8", newline="\n")
        zh_scripts[script_path] = (content, entries, source)
    missing = sorted(paths - set(zh_scripts))
    if missing:
        raise ValueError(f"Chinese localization missing for: {missing[:10]}")

    ja_files = {script_relative(path) for path in paths}
    zh_files = {script_relative(path) for path in zh_scripts}
    if ja_files != zh_files:
        raise ValueError("Japanese and Chinese path sets differ")
    ja_text = sum(len(tree["textMap"]["idToText"]["keys"]) for tree in ja_scripts)
    sample = {}
    for script_path, key in [
        ("Act01_Chapter01/Act01_Chapter01_Bad05", "0101Bad05_Ema001"),
        ("Act01_Chapter01/Act01_Chapter01_Adv07", "0101Adv07_Ema001"),
    ]:
        if script_path not in zh_scripts:
            continue
        zh_entry = dict(zh_scripts[script_path][1])
        sample[script_path] = {
            "ja": text_maps[script_path].get(key),
            "zh-Hans": zh_entry.get(key),
        }
    manifest = {
        "source": str(args.game.resolve().relative_to(ROOT)),
        "source_bundle_directory": str(STREAMING),
        "formats": {
            "ja": "Executable scenario scripts reconstructed from Naninovel Script data (.nani)",
            "zh-Hans": "Original Naninovel localization documents, not complete executable scenarios (.nani)",
        },
        "languages": {
            "ja": {"available": True, "scripts": len(ja_scripts)},
            "zh-Hans": {"available": True, "scripts": len(zh_scripts)},
            "en": {
                "available": False, "scripts": 0,
                "reason": "Not shipped by this game version (config.json lists ja and zh-Hans)",
            },
        },
        "ja_text_entries": ja_text,
        "zh_hans_translated_entries": translated_entries,
        "ja_source_bundles": len({source for items in sources.values() for source in items}),
        "zh_hans_source_bundles": len({item[2] for item in zh_scripts.values()}),
        # Every translated string resolves against the Japanese text map. A few
        # debug-only placeholder IDs have no corresponding Japanese text.
        "zh_hans_unmatched_text_ids": sum(item["count"] for item in unmatched),
        "zh_hans_unmatched_translated_ids": 0,
        "zh_hans_unmatched_documents": unmatched,
        "samples": sample,
    }
    args.out.mkdir(parents=True, exist_ok=True)
    report_path("scripts.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
