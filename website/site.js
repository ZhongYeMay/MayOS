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

for (const slot of document.querySelectorAll('.video-slot')) {
  fetch(slot.dataset.video, { method: 'HEAD' }).then(response => {
    if (!response.ok) return;
    const video = document.createElement('video');
    video.controls = true;
    video.preload = 'metadata';
    video.playsInline = true;
    video.setAttribute('aria-label', slot.dataset.label);
    const source = document.createElement('source');
    source.src = slot.dataset.video;
    source.type = 'video/mp4';
    video.append(source, document.createTextNode('浏览器不支持视频播放。'));
    slot.replaceChildren(video);
    const pending = slot.closest('article').querySelector('.recording-meta');
    if (pending?.dataset.meta) {
      const link = document.createElement('a');
      link.href = pending.dataset.meta;
      link.textContent = '验证元数据 ↗';
      pending.replaceWith(link);
    }
  }).catch(() => {});
}
