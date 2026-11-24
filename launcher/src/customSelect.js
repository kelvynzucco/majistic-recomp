// Custom HTML dropdown replacing native <select> popups.
// Native <select> popups crash the WebView2 browser process (0xC0000005) on
// some AMD/GPU setups (issue #6). The original <select> stays in the DOM
// (hidden) as the source of truth so `.value`, `.selectedIndex`, `.options`
// and `change` events keep working for the rest of the code.

const BTN_CLASS =
  "w-full h-[34px] flex items-center justify-between gap-2 bg-darkControl rounded-lg px-3 text-xs text-slate-200 text-left focus:outline-none cursor-pointer transition-colors duration-150 hover:bg-slate-800/80";
const LIST_CLASS =
  "fixed z-[9999] overflow-y-auto bg-darkCard/95 backdrop-blur-md rounded-xl shadow-2xl py-1 text-xs select-none";

// Crisp inline SVG flags (Windows WebView2 does not render Unicode regional-indicator flag emojis)
const FLAG_SVGS = {
  Auto: `<svg class="w-[18px] h-[13px] shrink-0 text-slate-400" viewBox="0 0 24 18" fill="none" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><circle cx="12" cy="9" r="7" stroke="currentColor" stroke-width="1.6"/><ellipse cx="12" cy="9" rx="3.2" ry="7" stroke="currentColor" stroke-width="1.3"/><line x1="5.2" y1="9" x2="18.8" y2="9" stroke="currentColor" stroke-width="1.3"/></svg>`,
  English: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="18" fill="#B22234"/><path d="M0 2.77H24M0 5.54H24M0 8.31H24M0 11.08H24M0 13.85H24M0 16.62H24" stroke="#FFFFFF" stroke-width="1.38"/><rect width="10.5" height="9.7" fill="#3C3B6E"/><g fill="#FFFFFF"><circle cx="2.1" cy="2" r="0.65"/><circle cx="4.5" cy="2" r="0.65"/><circle cx="6.9" cy="2" r="0.65"/><circle cx="9.1" cy="2" r="0.65"/><circle cx="3.3" cy="3.9" r="0.65"/><circle cx="5.7" cy="3.9" r="0.65"/><circle cx="8.1" cy="3.9" r="0.65"/><circle cx="2.1" cy="5.8" r="0.65"/><circle cx="4.5" cy="5.8" r="0.65"/><circle cx="6.9" cy="5.8" r="0.65"/><circle cx="9.1" cy="5.8" r="0.65"/><circle cx="3.3" cy="7.7" r="0.65"/><circle cx="5.7" cy="7.7" r="0.65"/><circle cx="8.1" cy="7.7" r="0.65"/></g></svg>`,
  Portuguese: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="18" fill="#DA291C"/><rect width="9.6" height="18" fill="#046A38"/><circle cx="9.6" cy="9" r="3.4" fill="#FFE900" stroke="#046A38" stroke-width="0.4"/><path d="M8.1 7.4H11.1V9.7C11.1 10.6 10.4 11.2 9.6 11.4C8.8 11.2 8.1 10.6 8.1 9.7V7.4Z" fill="#DA291C"/><rect x="8.8" y="8.1" width="1.6" height="2.2" rx="0.3" fill="#FFFFFF"/></svg>`,
  Spanish: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="18" fill="#AA151B"/><rect y="4.5" width="24" height="9" fill="#F1BF00"/><rect x="5.2" y="6.8" width="2.8" height="4.2" rx="0.6" fill="#AA151B"/><rect x="5.8" y="7.4" width="1.6" height="1.5" fill="#F1BF00"/></svg>`,
  French: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="8" height="18" fill="#002395"/><rect x="8" width="8" height="18" fill="#FFFFFF"/><rect x="16" width="8" height="18" fill="#ED2939"/></svg>`,
  German: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="6" fill="#111111"/><rect y="6" width="24" height="6" fill="#DD0000"/><rect y="12" width="24" height="6" fill="#FFCE00"/></svg>`,
  Italian: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="8" height="18" fill="#009246"/><rect x="8" width="8" height="18" fill="#FFFFFF"/><rect x="16" width="8" height="18" fill="#CE2B37"/></svg>`,
  Dutch: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="6" fill="#AE1C28"/><rect y="6" width="24" height="6" fill="#FFFFFF"/><rect y="12" width="24" height="6" fill="#21468B"/></svg>`,
  Russian: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="6" fill="#FFFFFF"/><rect y="6" width="24" height="6" fill="#0039A6"/><rect y="12" width="24" height="6" fill="#D52B1E"/></svg>`,
  Korean: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="18" fill="#FFFFFF"/><circle cx="12" cy="9" r="3.8" fill="#CD2E3A"/><path d="M12 12.8A3.8 3.8 0 0 1 8.2 9A1.9 1.9 0 0 1 12 9A1.9 1.9 0 0 0 15.8 9A3.8 3.8 0 0 1 12 12.8Z" fill="#0047A0"/><g stroke="#111111" stroke-width="1.1"><path d="M4.2 5.4L6.8 3.3M5 6.4L7.6 4.3"/><path d="M17.2 3.3L19.8 5.4M16.4 4.3L19 6.4"/><path d="M4.2 12.6L6.8 14.7M5 11.6L7.6 13.7"/><path d="M17.2 14.7L19.8 12.6M16.4 13.7L19 11.6"/></g></svg>`,
  Japanese: `<svg class="w-[18px] h-[13px] rounded-[2px] shrink-0 ring-1 ring-white/15" viewBox="0 0 24 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="24" height="18" fill="#FFFFFF"/><circle cx="12" cy="9" r="4.8" fill="#BC002D"/></svg>`,
};

function renderOptionContent(container, opt) {
  const flagKey = opt?.dataset?.flag;
  const svg = flagKey ? FLAG_SVGS[flagKey] : null;
  if (svg) {
    container.innerHTML = svg;
    const textSpan = document.createElement("span");
    textSpan.className = "truncate";
    textSpan.textContent = opt.textContent;
    container.appendChild(textSpan);
  } else {
    container.textContent = opt ? opt.textContent : "";
  }
}

let openInstance = null;
let exitingList = null;

function cleanupExitingList() {
  if (exitingList) {
    exitingList.remove();
    exitingList = null;
  }
}

function closeOpen(immediate = false) {
  if (immediate) {
    cleanupExitingList();
  }
  if (openInstance) {
    openInstance.close(immediate);
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
  label.className = "flex items-center gap-2 min-w-0 truncate";
  const arrow = document.createElement("span");
  arrow.className =
    "text-slate-400 text-[9px] transition-transform duration-200 shrink-0 inline-block pointer-events-none";
  arrow.textContent = "▼";
  btn.append(label, arrow);
  wrapper.appendChild(btn);

  let list = null;

  function refresh() {
    const opt = select.options[select.selectedIndex];
    renderOptionContent(label, opt);
  }

  function close(immediate = false) {
    arrow.style.transform = "rotate(0deg)";
    if (!list) return;
    cleanupExitingList();
    const currentList = list;
    list = null;
    if (immediate) {
      currentList.remove();
    } else {
      exitingList = currentList;
      currentList.classList.remove("dropdown-anim-down", "dropdown-anim-up");
      currentList.classList.add("dropdown-anim-exit");
      setTimeout(() => {
        if (exitingList === currentList) {
          cleanupExitingList();
        }
      }, 100);
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
    closeOpen(true);
    list = document.createElement("div");
    list.className = LIST_CLASS;
    Array.from(select.options).forEach((o, i) => {
      if (o.hidden || o.disabled) return;
      const item = document.createElement("div");
      const active = i === select.selectedIndex;
      item.className =
        "flex items-center gap-2 px-2.5 py-1.5 cursor-pointer hover:bg-darkControl/80 rounded-lg mx-1 my-0.5 transition-colors duration-150 " +
        (active ? "text-accent font-semibold" : "text-slate-200");
      renderOptionContent(item, o);
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
    if (openUp) {
      list.style.bottom = window.innerHeight - r.top + 2 + "px";
      list.classList.add("dropdown-anim-up");
    } else {
      list.style.top = r.bottom + 2 + "px";
      list.classList.add("dropdown-anim-down");
    }

    arrow.style.transform = "rotate(180deg)";

    openInstance = { close, list, btn };
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
  select._customSelectRefresh = refresh;
  refresh();
}

export function refreshCustomSelect(select) {
  if (select && select._customSelectRefresh) {
    select._customSelectRefresh();
  }
}

export function initCustomSelects() {
  document.querySelectorAll("select").forEach(enhance);
  document.addEventListener("mousedown", (e) => {
    if (!openInstance) return;
    if (openInstance.list && (openInstance.list === e.target || openInstance.list.contains(e.target))) {
      return;
    }
    if (openInstance.btn && (openInstance.btn === e.target || openInstance.btn.contains(e.target))) {
      return;
    }
    closeOpen();
  });
  window.addEventListener("blur", closeOpen);
  window.addEventListener("resize", closeOpen);
  document.addEventListener(
    "scroll",
    (e) => {
      if (!openInstance) return;
      // Do not close when scrolling inside the dropdown list itself
      if (openInstance.list && (openInstance.list === e.target || openInstance.list.contains(e.target))) {
        return;
      }
      closeOpen();
    },
    true
  );
}
