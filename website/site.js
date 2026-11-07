'use strict';
const toggle = document.querySelector('#theme');
let theme;
try { theme = localStorage.getItem('mayos-theme'); } catch {}
if (theme === 'dark' || theme === 'light') document.documentElement.dataset.theme = theme;
toggle.addEventListener('click', () => {
  const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  toggle.setAttribute('aria-label', next === 'dark' ? '切换浅色主题' : '切换深色主题');
  try { localStorage.setItem('mayos-theme', next); } catch {}
});
for (const button of document.querySelectorAll('[data-copy]')) {
  button.addEventListener('click', async () => {
    const status = document.querySelector('#copy-status');
    try {
      await navigator.clipboard.writeText(document.getElementById(button.dataset.copy).textContent.trim());
      status.textContent = 'SHA-256 已复制。';
      button.textContent = '已复制 ✓';
    } catch { status.textContent = '无法自动复制，请选中上方 SHA-256 手动复制。'; }
  });
}
