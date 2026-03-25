#!/usr/bin/env python
import ast
import sys

FNV1_32_INIT = 0x811c9dc5
FNV_32_PRIME = 0x01000193

def fnv1a_32(data, hval=FNV1_32_INIT):
    for byte in data:
        hval ^= byte
        hval = (hval * FNV_32_PRIME) & 0xffffffff
    return hval

def parse_po_quoted(text):
    return ast.literal_eval(text)

def new_entry():
    return {
        "flags": set(),
        "msgctxt": [],
        "msgid": [],
        "msgid_plural": [],
        "msgstr": {},
        "current_field": None,
        "current_index": None,
    }

def finalize_entry(entry, units, includefuzzy, encoding):
    if entry is None:
        return

    flags = entry["flags"]
    is_fuzzy = "fuzzy" in flags
    msgid = "".join(entry["msgid"])
    msgctxt = "".join(entry["msgctxt"])
    msgstr = "".join(entry["msgstr"].get(0, []))

    if not (msgstr and (not is_fuzzy or includefuzzy)):
        return

    source = msgid
    if msgctxt:
        source = msgctxt + '\004' + source

    hash_value = fnv1a_32(source.encode(encoding))
    units[hash_value] = msgstr.encode(encoding) + bytes(2)

def parse_po_entries(text):
    units = []
    entry = None

    for raw_line in text.splitlines():
        line = raw_line.strip()

        if not line:
            if entry is not None:
                units.append(entry)
                entry = None
            continue

        if line.startswith("#,"):
            if entry is None:
                entry = new_entry()
            entry["flags"].update(flag.strip() for flag in line[2:].split(",") if flag.strip())
            continue

        if line.startswith("#"):
            continue

        if line.startswith("msgctxt "):
            if entry is None:
                entry = new_entry()
            entry["msgctxt"] = [parse_po_quoted(line[8:])]
            entry["current_field"] = "msgctxt"
            entry["current_index"] = None
            continue

        if line.startswith("msgid_plural "):
            if entry is None:
                entry = new_entry()
            entry["msgid_plural"] = [parse_po_quoted(line[13:])]
            entry["current_field"] = "msgid_plural"
            entry["current_index"] = None
            continue

        if line.startswith("msgid "):
            if entry is not None and (entry["msgid"] or entry["msgstr"]):
                units.append(entry)
                entry = None
            if entry is None:
                entry = new_entry()
            entry["msgid"] = [parse_po_quoted(line[6:])]
            entry["current_field"] = "msgid"
            entry["current_index"] = None
            continue

        if line.startswith("msgstr["):
            if entry is None:
                entry = new_entry()
            closing_bracket = line.index("]")
            index = int(line[7:closing_bracket])
            value = parse_po_quoted(line[closing_bracket + 2:])
            entry["msgstr"][index] = [value]
            entry["current_field"] = "msgstr"
            entry["current_index"] = index
            continue

        if line.startswith("msgstr "):
            if entry is None:
                entry = new_entry()
            entry["msgstr"][0] = [parse_po_quoted(line[7:])]
            entry["current_field"] = "msgstr"
            entry["current_index"] = 0
            continue

        if line.startswith('"'):
            if entry is None or entry["current_field"] is None:
                raise ValueError(f"Unexpected continuation line: {raw_line}")

            value = parse_po_quoted(line)
            if entry["current_field"] == "msgstr":
                entry["msgstr"].setdefault(entry["current_index"], []).append(value)
            else:
                entry[entry["current_field"]].append(value)
            continue

        raise ValueError(f"Unsupported PO line: {raw_line}")

    if entry is not None:
        units.append(entry)

    return units

def po2ymo(infile, outfile, includefuzzy=False, encoding='utf-16le'):
    units = {}
    text = infile.read().decode('utf-8-sig')
    for entry in parse_po_entries(text):
        finalize_entry(entry, units, includefuzzy, encoding)

    byteorder='little'
    outfile.write(len(units).to_bytes(2, byteorder)) # len

    offset = 2 + len(units) * (4 + 2)
    for hash, data in units.items():
        outfile.write(hash.to_bytes(4, byteorder))
        outfile.write(offset.to_bytes(2, byteorder))
        offset += len(data)

    for data in units.values():
        outfile.write(data)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("uasge: po2ymo.py <infile> <outfile>")
        sys.exit()
    infile = open(sys.argv[1], 'rb')
    outfile = open(sys.argv[2], 'wb')
    po2ymo(infile, outfile)
