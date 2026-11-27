(() => {
  if (typeof window.__xncZapperCleanup === 'function') {
    window.__xncZapperCleanup();
    return 'off';
  }

  const overlay = document.createElement('div');
  overlay.id = '__xnc-zapper-overlay';
  Object.assign(overlay.style, {
    position: 'fixed',
    pointerEvents: 'none',
    border: '2px solid #ef4444',
    background: 'rgba(239, 68, 68, 0.14)',
    borderRadius: '4px',
    zIndex: '2147483646',
    display: 'none',
    boxSizing: 'border-box',
    transition: 'all 40ms ease-out'
  });

  const hud = document.createElement('div');
  hud.id = '__xnc-zapper-hud';
  Object.assign(hud.style, {
    position: 'fixed',
    bottom: '14px',
    left: '50%',
    transform: 'translateX(-50%)',
    padding: '7px 14px',
    borderRadius: '999px',
    background: 'rgba(20, 20, 24, 0.88)',
    color: '#f8fafc',
    font: '600 12px/1.3 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
    boxShadow: '0 8px 28px rgba(0,0,0,0.35)',
    border: '1px solid rgba(255,255,255,0.16)',
    backdropFilter: 'blur(12px)',
    zIndex: '2147483647',
    pointerEvents: 'none',
    whiteSpace: 'nowrap'
  });
  hud.textContent = 'XNC Zapper · Click: Zap | Shift+Click: Hide Always | U: Undo | Esc: Done';

  document.documentElement.appendChild(overlay);
  document.documentElement.appendChild(hud);

  let currentTarget = null;
  const undoStack = [];

  function escapeIdent(s) {
    return (typeof CSS !== 'undefined' && CSS.escape)
      ? CSS.escape(s)
      : String(s).replace(/[^a-zA-Z0-9_-]/g, '\\$&');
  }

  function describeElement(el) {
    if (!el || el.nodeType !== 1) return '';
    let desc = el.tagName.toLowerCase();
    if (el.id) desc += '#' + el.id;
    if (el.classList && el.classList.length) {
      desc += '.' + Array.from(el.classList).slice(0, 2).join('.');
    }
    return desc;
  }

  function computeSelector(el) {
    if (!el || el.nodeType !== 1) return '';
    if (el.id) return '#' + escapeIdent(el.id);
    const parts = [];
    let cur = el;
    for (let depth = 0; cur && cur.nodeType === 1 && depth < 4; depth++) {
      let sel = cur.tagName.toLowerCase();
      if (cur.id) {
        parts.unshift('#' + escapeIdent(cur.id));
        break;
      }
      if (cur.classList && cur.classList.length) {
        const classes = Array.from(cur.classList).filter(Boolean).slice(0, 2);
        if (classes.length) {
          sel += '.' + classes.map(escapeIdent).join('.');
        }
      }
      const parent = cur.parentElement;
      if (parent) {
        const siblings = Array.from(parent.children).filter(c => c.tagName === cur.tagName);
        if (siblings.length > 1) {
          sel += ':nth-of-type(' + (siblings.indexOf(cur) + 1) + ')';
        }
      }
      parts.unshift(sel);
      cur = parent;
    }
    return parts.join(' > ');
  }

  function persistRule(host, selector) {
    if (!selector) return;
    const style = document.createElement('style');
    style.setAttribute('data-xnc-zapper', '1');
    style.textContent = selector + ' { display: none !important; }';
    (document.head || document.documentElement).appendChild(style);

    const payload = JSON.stringify({ type: 'zapper', host: host || '', selector });
    try {
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.xncZapper) {
        window.webkit.messageHandlers.xncZapper.postMessage(payload);
      } else if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(payload);
      }
    } catch (_) {}
  }

  function onMove(e) {
    const el = document.elementFromPoint(e.clientX, e.clientY);
    if (!el || el === overlay || el === hud || el === document.documentElement || el === document.body) {
      overlay.style.display = 'none';
      currentTarget = null;
      return;
    }
    currentTarget = el;
    const r = el.getBoundingClientRect();
    Object.assign(overlay.style, {
      display: 'block',
      left: r.left + 'px',
      top: r.top + 'px',
      width: Math.max(2, r.width) + 'px',
      height: Math.max(2, r.height) + 'px',
      borderColor: e.shiftKey ? '#f59e0b' : '#ef4444',
      background: e.shiftKey ? 'rgba(245, 158, 11, 0.16)' : 'rgba(239, 68, 68, 0.14)'
    });
    hud.textContent =
      describeElement(el) + ' · ' + Math.round(r.width) + 'x' + Math.round(r.height) +
      ' — Click: Zap | Shift+Click: Hide Always | U: Undo | Esc: Exit';
  }

  function onClick(e) {
    e.preventDefault();
    e.stopImmediatePropagation();
    const target = currentTarget;
    if (!target || !target.parentNode) return;

    if (e.shiftKey) {
      const sel = computeSelector(target);
      persistRule(window.location.hostname, sel);
      hud.textContent = 'Saved rule: ' + sel + ' (Esc to exit)';
    } else {
      undoStack.push({ el: target, parent: target.parentNode, next: target.nextSibling });
      target.remove();
      overlay.style.display = 'none';
      currentTarget = null;
    }
  }

  function onKey(e) {
    if (e.key === 'Escape') {
      e.preventDefault();
      cleanup();
    } else if ((e.key === 'u' || e.key === 'U') && !e.metaKey && !e.ctrlKey) {
      const item = undoStack.pop();
      if (item && item.parent) {
        item.parent.insertBefore(item.el, item.next || null);
      }
    }
  }

  function cleanup() {
    document.removeEventListener('mousemove', onMove, true);
    document.removeEventListener('click', onClick, true);
    document.removeEventListener('keydown', onKey, true);
    overlay.remove();
    hud.remove();
    delete window.__xncZapperCleanup;
  }

  window.__xncZapperCleanup = cleanup;
  document.addEventListener('mousemove', onMove, true);
  document.addEventListener('click', onClick, true);
  document.addEventListener('keydown', onKey, true);
  return 'on';
})();
