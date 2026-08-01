# LLocr

![Status](https://img.shields.io/badge/status-MVP%20in%20development-orange)
![License](https://img.shields.io/badge/license-GPLv3-blue)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

**A cross-platform application for OCR based on local LLMs.**

LLocr connects to a local LLM provider (llama.cpp / OpenAI-compatible API),
recognizes text from images and PDFs, and exports the result to Markdown, DOCX, and more —
all running locally, without sending your data to the cloud.

---

## ✨ Features

> ⚠️ The project is under active development. Items marked *(WIP)* are not yet complete.

- 🖥️ **Cross-platform**: Windows, macOS, Linux
- 🧠 Support for different OCR models with their own input/output formats
- 📄 Export to **Markdown, TXT, DOCX, PDF, HTML**
- 🔒 Fully local processing — your data never leaves your machine
- 📚 **RAG integration** to grow a knowledge base from scans *(WIP)*

## 🛠️ Tech Stack

| Area              | Technologies                          |
|-------------------|---------------------------------------|
| Core / UI         | Qt6 · C++ · QML                       |
| Build system      | CMake · vcpkg                         |
| RAG service       | Python                                |

## 🚀 Getting Started

### Prerequisites
- Qt 6.x
- CMake ≥ 3.21
- A C++17-compatible compiler
- vcpkg
- Python 3.x (for the RAG service)
- A running local LLM provider (e.g. [llama.cpp](https://github.com/ggerganov/llama.cpp) or any OpenAI-compatible endpoint)
