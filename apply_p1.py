#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# clean_patch_labels.py - удаляет метки патчей (p15:, p20 v2: и т.д.) из комментариев.
# Проходится по файлам, изменённым за последние 2 дня. Запуск: py clean_patch_labels.py
import os, re, subprocess

dst = "."

# Коммиты за последние 2 дня
r = subprocess.run(
    ["git", "log", "--since=2 days", "--name-only", "--pretty=format:"],
    capture_output=True, text=True
)

# Извлекаем уникальные файлы (только .c и .h)
files_to_clean = set()
for line in r.stdout.strip().split('\n'):
    f = line.strip()
    if f and (f.endswith('.c') or f.endswith('.h')):
        files_to_clean.add(f)

print(f"Found {len(files_to_clean)} files to process\n")

def clean_patch_labels(text):
    """Удаляет метки патчей (p15:, p20 v2: и т.д.) из комментариев"""
    # /* p15: текст */ -> /* текст */
    text = re.sub(r'/\*\s*p\d+(?:\s+v\d+)?\s*:\s*', '/* ', text)
    # // p15: текст -> // текст
    text = re.sub(r'//\s*p\d+(?:\s+v\d+)?\s*:\s*', '// ', text)
    # /* текст, p15: ещё текст */ -> /* текст, ещё текст */
    text = re.sub(r',\s*p\d+(?:\s+v\d+)?\s*:\s*', ', ', text)
    # "     p15: текст" -> "     текст"
    text = re.sub(r'^(\s*)p\d+(?:\s+v\d+)?\s*:\s*', r'\1', text, flags=re.MULTILINE)
    # ; p15: текст -> ; текст
    text = re.sub(r';\s*p\d+(?:\s+v\d+)?\s*:\s*', '; ', text)
    return text

total_changes = 0
for f in sorted(files_to_clean):
    if not os.path.exists(f):
        continue
    
    with open(f, encoding="utf-8") as fp:
        content = fp.read()
    
    cleaned = clean_patch_labels(content)
    
    if cleaned != content:
        orig_lines = content.split('\n')
        clean_lines = cleaned.split('\n')
        changes = sum(1 for a,b in zip(orig_lines, clean_lines) if a!=b)
        
        print(f"[+] {f}: {changes} строк очищено")
        
        with open(f, "w", encoding="utf-8", newline="") as fp:
            fp.write(cleaned)
        total_changes += changes
    else:
        print(f"[=] {f}: без изменений")

print(f"\n✓ Всего: {total_changes} строк очищено от меток патчей")
print("ГОТОВО. Теперь сделай git add + commit.")