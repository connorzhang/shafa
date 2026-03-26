import pypdf
import os
import sys

try:
    pdf_path = "沙发+-+机智云平台标准接入协议之MCU与蓝牙通讯模组通讯 (1).pdf"
    txt_path = "extracted_pdf.txt"
    print(f"Reading {pdf_path}...")
    with open(pdf_path, "rb") as f:
        reader = pypdf.PdfReader(f)
        text = ""
        for i, page in enumerate(reader.pages):
            print(f"Reading page {i+1}...")
            text += page.extract_text() + "\n---PAGE_BREAK---\n"
    
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write(text)
    print("Extraction complete")
except Exception as e:
    print(f"Error: {e}")
