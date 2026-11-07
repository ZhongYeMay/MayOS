'use strict';
const root = document.documentElement;
const themeButton = document.getElementById('theme');
const menuButton = document.getElementById('menu');
const nav = document.getElementById('nav');
let storedTheme = null;
try { storedTheme = localStorage.getItem('mayos-theme'); } catch (_) {}
const preferredDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
const initial = storedTheme === 'dark' || storedTheme === 'light' ? storedTheme : (preferredDark ? 'dark' : 'light');
root.dataset.theme = initial;
function setTheme(next) {
  root.dataset.theme = next;
  themeButton?.setAttribute('aria-label', next === 'dark' ? '切换浅色主题' : '切换深色主题');
  try { localStorage.setItem('mayos-theme', next); } catch (_) {}
}
setTheme(initial);
themeButton?.addEventListener('click', () => setTheme(root.dataset.theme === 'dark' ? 'light' : 'dark'));
menuButton?.addEventListener('click', () => {
  const isOpen = nav.classList.toggle('open');
  menuButton.setAttribute('aria-expanded', String(isOpen));
  menuButton.setAttribute('aria-label', isOpen ? '收起导航' : '展开导航');
});
nav?.querySelectorAll('a').forEach(link => link.addEventListener('click', () => {
  nav.classList.remove('open');
  menuButton?.setAttribute('aria-expanded', 'false');
}));
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && nav?.classList.contains('open')) {
    nav.classList.remove('open');
    menuButton?.setAttribute('aria-expanded', 'false');
    menuButton?.focus();
  }
});
const reduceMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
if (reduceMotion || !('IntersectionObserver' in window)) {
  document.querySelectorAll('.reveal').forEach(el => el.classList.add('visible'));
} else {
  const observer = new IntersectionObserver(entries => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        entry.target.classList.add('visible');
        observer.unobserve(entry.target);
      }
    }
  }, { threshold: .08, rootMargin: '0px 0px 40px 0px' });
  document.querySelectorAll('.reveal').forEach(el => observer.observe(el));
}
document.querySelectorAll('[data-copy]').forEach(button => {
  button.addEventListener('click', async () => {
    const target = document.getElementById(button.dataset.copy);
    const status = document.getElementById('copy-status');
    if (!target) return;
    try {
      await navigator.clipboard.writeText(target.textContent.trim());
      button.textContent = '已复制';
      if (status) status.textContent = 'SHA-256 已复制到剪贴板。';
    } catch (_) {
      if (status) status.textContent = '复制失败，请手动选择并复制校验值。';
    }
  });
});
