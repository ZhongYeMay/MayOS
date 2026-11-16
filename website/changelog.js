'use strict';
/* One readable source of truth: public GitHub Releases.
 * release-history.json is a checked, dated snapshot for offline/API failures.
 * Never treat a development branch as a published release. */
(() => {
  const ROOT = 'https://api.github.com/repos/ZhongYeMay/MayOS/releases';
  const fallback = 'release-history.json';
  const list = document.getElementById('release-list');
  const status = document.getElementById('log-status');
  const source = document.getElementById('log-source');
  const count = document.getElementById('log-count');
  const search = document.getElementById('log-search');
  const empty = document.getElementById('log-empty');
  const filters = [...document.querySelectorAll('[data-filter]')];
  let releases = [], filter = 'all', sourceName = '';

  const normalize = item => ({
    tag: String(item.tag || item.tag_name || ''),
    name: String(item.name || item.tag || item.tag_name || ''),
    body: String(item.body || ''),
    published_at: item.published_at || '',
    prerelease: Boolean(item.prerelease),
    url: String(item.url || item.html_url || ''),
    assets_count: Number(item.assets_count ?? item.assets?.length ?? 0)
  });
  const validate = items => items.filter(item =>
    item.tag && item.url.startsWith('https://github.com/ZhongYeMay/MayOS/releases/tag/') &&
    item.published_at && Number.isFinite(Date.parse(item.published_at))
  );
  const date = iso => new Intl.DateTimeFormat('zh-CN', {
    year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC'
  }).format(new Date(iso));
  const el = (tag, cls, value) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (value !== undefined) node.textContent = value;
    return node;
  };
  const classify = item => item.prerelease ? 'preview' : 'stable';

  function appendNotes(target, markdown) {
    if (!markdown.trim()) {
      target.append(el('p', 'update-note', '该版本没有填写发布说明，可点击原始 Release 查看附件。'));
      return;
    }
    let ul = null;
    for (const raw of markdown.replace(/\r\n/g, '\n').split('\n')) {
      const line = raw.trim();
      if (!line) { ul = null; continue; }
      const cleaned = line.replace(/\*\*/g, '').replace(/(?<!\w)[\x60]/g, '');
      const heading = /^(#{1,4})\s+(.*)$/.exec(cleaned);
      const item = /^[-*]\s+(.+)$/.exec(cleaned);
      if (heading) {
        ul = null;
        target.append(el('h4', 'update-note-heading', heading[2]));
      } else if (item) {
        if (!ul) { ul = el('ul', 'update-note-list'); target.append(ul); }
        ul.append(el('li', '', item[1]));
      } else {
        ul = null;
        target.append(el('p', 'update-note', cleaned));
      }
    }
  }

  function card(item, index) {
    const article = el('article', 'update-entry');
    article.id = item.tag;  // stable link: changelog.html#v0.5.0-pre1
    const eyebrow = el('div', 'update-entry-meta');
    const chip = el('span', 'update-entry-chip', item.prerelease ? '预览版 / Preview' : '正式版 / Release');
    eyebrow.append(chip, el('time', '', date(item.published_at)));
    const tag = el('span', 'update-entry-tag', item.tag);
    const title = el('h3', 'update-entry-title', item.name);
    const details = el('details', 'update-entry-details');
    if (index === 0) details.open = true;
    const summary = el('summary', 'update-entry-summary', '查看本次更新内容');
    const notes = el('div', 'update-entry-notes');
    appendNotes(notes, item.body);
    const actions = el('div', 'update-entry-actions');
    const link = el('a', 'link', '查看 GitHub 原始发布记录');
    link.href = item.url;
    link.target = '_blank';
    link.rel = 'noopener noreferrer';
    const share = el('a', 'update-anchor', '版本直达链接');
    share.href = '#' + encodeURIComponent(item.tag);
    actions.append(link, share);
    if (item.assets_count) {
      actions.append(el('span', 'update-entry-assets', item.assets_count + ' 个发布附件'));
    }
    details.append(summary, notes, actions);
    article.append(eyebrow, tag, title, details);
    return article;
  }

  function render() {
    const keyword = search.value.trim().toLocaleLowerCase();
    const results = releases.filter(item =>
      (filter === 'all' || classify(item) === filter) &&
      (!keyword || (item.tag + ' ' + item.name + ' ' + item.body).toLocaleLowerCase().includes(keyword))
    );
    list.replaceChildren(...results.map((item, i) => card(item, i)));
    empty.hidden = results.length !== 0;
    count.textContent = releases.length + ' 个版本';
    status.textContent = sourceName + ' · 正在显示 ' + results.length + ' / ' + releases.length + ' 个版本';
    if (location.hash) {
      const tag = decodeURIComponent(location.hash.slice(1));
      const target = document.getElementById(tag);
      if (target) {
        const details = target.querySelector('details');
        if (details) details.open = true;
      }
    }
  }

  function setData(items, label) {
    const deduped = new Map();
    for (const raw of items.map(normalize)) {
      if (!deduped.has(raw.tag)) deduped.set(raw.tag, raw);
    }
    releases = validate([...deduped.values()]).sort((a, b) =>
      Date.parse(b.published_at) - Date.parse(a.published_at)
    );
    sourceName = label;
    source.textContent = label;
    render();
  }

  filters.forEach(button => button.addEventListener('click', () => {
    filter = button.dataset.filter;
    filters.forEach(other => {
      other.classList.toggle('is-active', other === button);
      other.setAttribute('aria-pressed', String(other === button));
    });
    render();
  }));
  search.addEventListener('input', render);
  window.addEventListener('hashchange', render);

  async function load() {
    try {
      const res = await fetch(fallback, { cache: 'no-cache' });
      if (!res.ok) throw new Error('archive not found');
      const snapshot = await res.json();
      if (snapshot.schema_version !== 1 || !Array.isArray(snapshot.releases))
        throw new Error('archive format invalid');
      setData(snapshot.releases, '历史快照 · ' + snapshot.snapshot_date);
    } catch (_) {
      status.textContent = '本地历史快照不可用，正在读取 GitHub Releases…';
    }

    try {
      const all = [];
      // Cover the entire public history rather than the first ten releases.
      for (let page = 1; page <= 20; page++) {
        const response = await fetch(ROOT + '?per_page=100&page=' + page, {
          headers: { Accept: 'application/vnd.github+json' },
          cache: 'no-cache'
        });
        if (!response.ok) throw new Error('GitHub API HTTP ' + response.status);
        const batch = await response.json();
        if (!Array.isArray(batch)) throw new Error('Invalid release API result');
        all.push(...batch);
        if (batch.length < 100) break;
      }
      if (!all.length) throw new Error('Empty release history');
      setData(all, 'GitHub 实时公开记录');
    } catch (_) {
      if (!releases.length) {
        source.textContent = '暂不可用';
        status.textContent = '加载失败。可前往 GitHub Releases 查看所有版本。';
        const link = el('a', 'link', '打开 GitHub Releases');
        link.href = 'https://github.com/ZhongYeMay/MayOS/releases';
        list.replaceChildren(link);
      } else {
        status.textContent = 'GitHub 暂不可用；已展示 ' + releases.length +
          ' 条保存的历史快照，可能不包含刚发布的版本。';
      }
    }
  }
  load();
})();
