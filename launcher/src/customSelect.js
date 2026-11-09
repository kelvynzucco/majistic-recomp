// Custom HTML dropdown replacing native <select> popups.
// Native <select> popups crash the WebView2 browser process (0xC0000005) on
// some AMD/GPU setups (issue #6). The original <select> stays in the DOM
// (hidden) as the source of truth so `.value`, `.selectedIndex`, `.options`
// and `change` events keep working for the rest of the code.

const BTN_CLASS =
  "w-full flex items-center justify-between gap-2 bg-darkControl border border-darkBorder rounded px-2.5 py-1.5 text-xs text-slate-200 text-left focus:outline-none focus:border-accent cursor-pointer";
const LIST_CLASS =
  "fixed z-[9999] overflow-y-auto bg-darkCard border border-darkBorder rounded shadow-lg py-1 text-xs";

let openInstance = null;

function closeOpen() {
  if (openInstance) {
    openInstance.close();
    openInstance = null;
  }
}

function enhance(select) {
  if (select.dataset.customSelect) return;
  select.dataset.customSelect = "1";

  const wrapper = document.createElement("div");
  wrapper.className = "relative";
  select.parentNode.insertBefore(wrapper, select);
  wrapper.appendChild(select);
  select.style.display = "none";

  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = BTN_CLASS;
  const label = document.createElement("span");
  label.className = "truncate";
  const arrow = document.createElement("span");
  arrow.className = "text-slate-400 text-[9px]";
  arrow.textContent = "▼";
  btn.append(label, arrow);
  wrapper.appendChild(btn);

  let list = null;

  function refresh() {
    const opt = select.options[select.selectedIndex];
    label.textContent = opt ? opt.textContent : "";
  }

  function close() {
    if (list) {
      list.remove();
      list = null;
    }
  }

  function choose(index) {
    if (select.selectedIndex !== index) {
      select.selectedIndex = index;
      select.dispatchEvent(new Event("change", { bubbles: true }));
    }
    refresh();
    closeOpen();
  }

  function open() {
    closeOpen();
    list = document.createElement("div");
    list.className = LIST_CLASS;
    Array.from(select.options).forEach((o, i) => {
      const item = document.createElement("div");
      const active = i === select.selectedIndex;
      item.className =
        "px-2.5 py-1.5 cursor-pointer hover:bg-darkControl " +
        (active ? "text-accent font-semibold" : "text-slate-200");
      item.textContent = o.textContent;
      item.addEventListener("mousedown", (e) => {
        e.preventDefault();
        e.stopPropagation();
        choose(i);
      });
      list.appendChild(item);
    });
    document.body.appendChild(list);

    const r = btn.getBoundingClientRect();
    const maxH = 220;
    const below = window.innerHeight - r.bottom - 6;
    const above = r.top - 6;
    const openUp = below < Math.min(list.scrollHeight, maxH) && above > below;
    list.style.left = r.left + "px";
    list.style.width = r.width + "px";
    list.style.maxHeight = Math.min(maxH, openUp ? above : below) + "px";
    if (openUp) list.style.bottom = window.innerHeight - r.top + 2 + "px";
    else list.style.top = r.bottom + 2 + "px";

    openInstance = { close };
  }

  btn.addEventListener("click", (e) => {
    e.stopPropagation();
    if (list) closeOpen();
    else open();
  });
  btn.addEventListener("keydown", (e) => {
    if (e.key === "Escape") closeOpen();
  });

  // Keep label in sync when code sets value / selectedIndex programmatically.
  for (const prop of ["value", "selectedIndex"]) {
    const desc = Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, prop);
    Object.defineProperty(select, prop, {
      configurable: true,
      get() {
        return desc.get.call(this);
      },
      set(v) {
        desc.set.call(this, v);
        refresh();
      },
    });
  }
  select.addEventListener("change", refresh);
  refresh();
}

export function initCustomSelects() {
  document.querySelectorAll("select").forEach(enhance);
  document.addEventListener("mousedown", (e) => {
    if (openInstance && !e.target.closest(".fixed.z-\\[9999\\], button")) closeOpen();
  });
  window.addEventListener("blur", closeOpen);
  window.addEventListener("resize", closeOpen);
  document.addEventListener("scroll", closeOpen, true);
}
