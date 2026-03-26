#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageOps


CHUNK_SIZE = 1024 * 1024
MIN_OCR_ALNUM = 20
DEFAULT_MIN_IMAGE_PIXELS = 40_000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert PDFs to Markdown with text extraction and OCR of embedded images."
    )
    parser.add_argument(
        "inputs",
        nargs="+",
        help="PDF files or directories to scan recursively for PDFs.",
    )
    parser.add_argument(
        "--min-image-pixels",
        type=int,
        default=DEFAULT_MIN_IMAGE_PIXELS,
        help="Skip OCR for embedded images smaller than this pixel count.",
    )
    parser.add_argument(
        "--skip-ocr",
        action="store_true",
        help="Disable OCR even when tesseract is installed.",
    )
    return parser.parse_args()


def find_pdfs(inputs: Iterable[str]) -> list[Path]:
    pdfs: list[Path] = []
    for raw in inputs:
        path = Path(raw)
        if path.is_dir():
            pdfs.extend(sorted(p for p in path.rglob("*.pdf") if p.is_file()))
        elif path.is_file() and path.suffix.lower() == ".pdf":
            pdfs.append(path)
        else:
            raise FileNotFoundError(f"Input not found or not a PDF/directory: {raw}")
    unique = sorted(dict.fromkeys(p.resolve() for p in pdfs))
    return [Path(p) for p in unique]


def run_command(args: list[str], *, quiet: bool = False) -> str:
    completed = subprocess.run(
        args,
        check=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE if quiet else None,
    )
    return completed.stdout


def pdf_metadata(pdf_path: Path) -> dict[str, str]:
    info = run_command(["pdfinfo", str(pdf_path)], quiet=True)
    metadata: dict[str, str] = {}
    for line in info.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        metadata[key.strip()] = value.strip()
    return metadata


def export_pdf_text(pdf_path: Path, text_path: Path) -> None:
    subprocess.run(
        ["pdftotext", "-layout", "-enc", "UTF-8", str(pdf_path), str(text_path)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def iter_pages_from_text(text_path: Path) -> Iterable[str]:
    buffer = ""
    with text_path.open("r", encoding="utf-8", errors="replace") as handle:
        while True:
            chunk = handle.read(CHUNK_SIZE)
            if not chunk:
                break
            buffer += chunk
            while "\f" in buffer:
                page_text, buffer = buffer.split("\f", 1)
                yield page_text
    if buffer:
        yield buffer


def parse_pdfimages_list(pdf_path: Path, min_image_pixels: int) -> dict[int, list[dict[str, int]]]:
    output = run_command(["pdfimages", "-list", str(pdf_path)], quiet=True)
    pages: dict[int, list[dict[str, int]]] = defaultdict(list)
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("page") or stripped.startswith("---"):
            continue
        parts = stripped.split()
        if len(parts) < 5:
            continue
        if parts[2] != "image":
            continue
        try:
            page = int(parts[0])
            width = int(parts[3])
            height = int(parts[4])
        except ValueError:
            continue
        if width * height < min_image_pixels:
            continue
        pages[page].append({"width": width, "height": height})
    return dict(pages)


def clean_text(text: str) -> str:
    lines = [line.rstrip() for line in text.replace("\x00", "").splitlines()]
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines)


def preprocess_image(source: Path, destination: Path) -> None:
    with Image.open(source) as image:
        processed = ImageOps.grayscale(image)
        processed = ImageOps.autocontrast(processed)
        width, height = processed.size
        longest_edge = max(width, height)
        if longest_edge < 1800:
            scale = max(2, int(round(1800 / max(1, longest_edge))))
            processed = processed.resize(
                (width * scale, height * scale),
                getattr(Image, "Resampling", Image).LANCZOS,
            )
        processed.save(destination)


def is_meaningful_ocr(text: str) -> bool:
    return len(re.findall(r"[A-Za-z0-9]", text)) >= MIN_OCR_ALNUM


def ocr_page_images(pdf_path: Path, page_number: int, work_dir: Path) -> list[str]:
    prefix = work_dir / f"page"
    before = {path.name for path in work_dir.iterdir()}
    subprocess.run(
        [
            "pdfimages",
            "-f",
            str(page_number),
            "-l",
            str(page_number),
            "-p",
            "-png",
            str(pdf_path),
            str(prefix),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    new_images = sorted(
        path
        for path in work_dir.iterdir()
        if path.is_file() and path.suffix.lower() == ".png" and path.name not in before
    )
    ocr_blocks: list[str] = []
    seen: set[str] = set()
    for image_path in new_images:
        preprocessed = image_path.with_name(f"{image_path.stem}-ocr.png")
        preprocess_image(image_path, preprocessed)
        text = run_command(
            ["tesseract", str(preprocessed), "stdout", "--psm", "6", "-l", "eng"],
            quiet=True,
        )
        cleaned = clean_text(text)
        normalized = re.sub(r"\s+", " ", cleaned).strip().lower()
        if not is_meaningful_ocr(cleaned) or normalized in seen:
            image_path.unlink(missing_ok=True)
            preprocessed.unlink(missing_ok=True)
            continue
        seen.add(normalized)
        ocr_blocks.append(cleaned)
        image_path.unlink(missing_ok=True)
        preprocessed.unlink(missing_ok=True)
    return ocr_blocks


def write_code_block(handle, text: str) -> None:
    handle.write("```text\n")
    handle.write(text)
    if text and not text.endswith("\n"):
        handle.write("\n")
    handle.write("```\n")


def convert_pdf(pdf_path: Path, *, enable_ocr: bool, min_image_pixels: int) -> Path:
    metadata = pdf_metadata(pdf_path)
    output_path = pdf_path.with_suffix(".md")
    with tempfile.TemporaryDirectory(prefix="pdf-to-md-") as tmp:
        temp_dir = Path(tmp)
        text_path = temp_dir / "pages.txt"
        export_pdf_text(pdf_path, text_path)
        images_by_page = (
            parse_pdfimages_list(pdf_path, min_image_pixels) if enable_ocr else {}
        )

        with output_path.open("w", encoding="utf-8") as out:
            title = metadata.get("Title") or pdf_path.stem
            out.write(f"# {title}\n\n")
            out.write(f"- Source PDF: `{pdf_path.name}`\n")
            if metadata.get("Pages"):
                out.write(f"- Pages: {metadata['Pages']}\n")
            if enable_ocr:
                out.write("- OCR: embedded images processed with Tesseract\n")
            else:
                out.write("- OCR: skipped\n")
            out.write("\n")

            page_count = 0
            for page_count, page_text in enumerate(iter_pages_from_text(text_path), start=1):
                cleaned_page_text = clean_text(page_text)
                out.write(f"## Page {page_count}\n\n")
                if cleaned_page_text:
                    write_code_block(out, cleaned_page_text)
                else:
                    out.write("_No extractable text found on this page._\n")
                out.write("\n")

                if page_count in images_by_page:
                    ocr_blocks = ocr_page_images(pdf_path, page_count, temp_dir)
                    if ocr_blocks:
                        out.write("### OCR from embedded images\n\n")
                        for index, block in enumerate(ocr_blocks, start=1):
                            out.write(f"#### Image {index}\n\n")
                            write_code_block(out, block)
                            out.write("\n")

            declared_pages = metadata.get("Pages")
            if declared_pages and declared_pages.isdigit() and int(declared_pages) > page_count:
                for missing_page in range(page_count + 1, int(declared_pages) + 1):
                    out.write(f"## Page {missing_page}\n\n")
                    out.write("_No extractable text found on this page._\n\n")
                    if missing_page in images_by_page:
                        ocr_blocks = ocr_page_images(pdf_path, missing_page, temp_dir)
                        if ocr_blocks:
                            out.write("### OCR from embedded images\n\n")
                            for index, block in enumerate(ocr_blocks, start=1):
                                out.write(f"#### Image {index}\n\n")
                                write_code_block(out, block)
                                out.write("\n")
    return output_path


def main() -> int:
    args = parse_args()
    if shutil.which("pdftotext") is None or shutil.which("pdfimages") is None:
        print("Missing required tools: pdftotext and pdfimages must be installed.", file=sys.stderr)
        return 1

    enable_ocr = not args.skip_ocr
    if enable_ocr and shutil.which("tesseract") is None:
        print("Tesseract not found; continuing without OCR.", file=sys.stderr)
        enable_ocr = False

    try:
        pdfs = find_pdfs(args.inputs)
    except FileNotFoundError as error:
        print(str(error), file=sys.stderr)
        return 1

    if not pdfs:
        print("No PDF files found.", file=sys.stderr)
        return 1

    for index, pdf_path in enumerate(pdfs, start=1):
        print(f"[{index}/{len(pdfs)}] Converting {pdf_path}")
        output_path = convert_pdf(
            pdf_path,
            enable_ocr=enable_ocr,
            min_image_pixels=args.min_image_pixels,
        )
        print(f"    -> {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
