#!/usr/bin/env python3
"""图标生成器 — 从 icon_data.json 批量生成 light/dark SVG 文件."""
import json
import os

# SVG 模板
LIGHT_BG = '<rect width="32" height="32" rx="6" fill="#FFFFFF" stroke="#E8E8E8" stroke-width="1.0"/>'
DARK_BG  = '<rect width="32" height="32" rx="6" fill="#2A2A3A" stroke="#3A3A5A" stroke-width="1.0"/>'

HEADER = '<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">\n'
FOOTER = '</svg>\n'

def generate_icon(name, data, theme):
    """根据主题替换颜色占位符生成完整 SVG."""
    body = data["body"]
    if theme == "light":
        bg = LIGHT_BG
        body = body.replace("{{stroke}}", data["light_stroke"])
        body = body.replace("{{fill}}", data["light_fill"])
        body = body.replace("{{fill_opacity}}", data.get("light_fill_opacity", "0.85"))
    else:
        bg = DARK_BG
        body = body.replace("{{stroke}}", data["dark_stroke"])
        body = body.replace("{{fill}}", data["dark_fill"])
        body = body.replace("{{fill_opacity}}", data.get("dark_fill_opacity", "0.6"))
    return HEADER + "  " + bg + "\n" + body + FOOTER

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    data_path = os.path.join(script_dir, "icon_data.json")
    light_dir = os.path.join(project_root, "resources", "icons", "light")
    dark_dir = os.path.join(project_root, "resources", "icons", "dark")
    os.makedirs(light_dir, exist_ok=True)
    os.makedirs(dark_dir, exist_ok=True)

    with open(data_path, "r", encoding="utf-8") as f:
        icons = json.load(f)

    count = 0
    for icon in icons:
        name = icon["name"]
        # 浅色版
        light_svg = generate_icon(name, icon, "light")
        with open(os.path.join(light_dir, f"{name}.svg"), "w", encoding="utf-8") as f:
            f.write(light_svg)
        # 深色版
        dark_svg = generate_icon(name, icon, "dark")
        with open(os.path.join(dark_dir, f"{name}.svg"), "w", encoding="utf-8") as f:
            f.write(dark_svg)
        count += 2
    print(f"生成完成: {count} 个 SVG 文件")

if __name__ == "__main__":
    main()
