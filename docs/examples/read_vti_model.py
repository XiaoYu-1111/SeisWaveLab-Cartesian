"""Inspect an uncompressed VTI physical model using only the Python standard library.

For compressed/base64/multi-piece VTI use vtkXMLImageDataReader instead.
"""
import argparse
import array
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def inspect(path):
    with Path(path).open("rb") as source:
        # Only the XML header is buffered; binary fields are read one array at a time.
        prefix = source.read(1024 * 1024)
        marker = re.search(rb'<AppendedData\b[^>]*encoding="raw"[^>]*>\s*_', prefix)
        if marker:
            start = marker.end()
            root = ET.fromstring(prefix[:marker.start()] + b"</VTKFile>")
        else:
            source.seek(0)
            root = ET.parse(source).getroot()
            start = None
        if root.attrib.get("type") != "ImageData" or root.attrib.get("compressor"):
            raise ValueError("Expected uncompressed ImageData")
        image = root.find("ImageData")
        extent = list(map(int, image.attrib["WholeExtent"].split()))
        dims = [extent[i + 1] - extent[i] + 1 for i in (0, 2, 4)]
        count = dims[0] * dims[1] * dims[2]
        little = root.attrib.get("byte_order", "LittleEndian") == "LittleEndian"
        header = "Q" if root.attrib.get("header_type", "UInt32") == "UInt64" else "I"
        result = dict(dimensions=dims, origin=image.attrib.get("Origin", "0 0 0"),
                      spacing=image.attrib.get("Spacing", "1 1 1"), fields={})
        for item in image.findall("Piece/PointData/DataArray"):
            name = item.attrib.get("Name")
            if name not in ("Vp", "Vs", "Density"):
                continue
            code = {"Float32": "f", "Float64": "d"}[item.attrib["type"]]
            if item.attrib.get("format", "ascii") == "ascii":
                values = array.array(code, map(float, (item.text or "").split()))
            elif item.attrib.get("format") == "appended" and start is not None:
                source.seek(start + int(item.attrib["offset"]))
                size = struct.unpack(("<" if little else ">") + header,
                                     source.read(struct.calcsize(header)))[0]
                if size != count * array.array(code).itemsize:
                    raise ValueError("Incorrect array byte count")
                values = array.array(code)
                values.fromfile(source, count)
                if little != (sys.byteorder == "little"):
                    values.byteswap()
            else:
                raise ValueError("Use raw-appended or ASCII data")
            if len(values) != count:
                raise ValueError("Incorrect node count")
            result["fields"][name] = dict(count=len(values), minimum=min(values), maximum=max(values),
                                         first=values[0], last=values[-1])
        if set(result["fields"]) != {"Vp", "Vs", "Density"}:
            raise ValueError("Missing physical material arrays")
        return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model")
    print(json.dumps(inspect(parser.parse_args().model), ensure_ascii=False, indent=2))
