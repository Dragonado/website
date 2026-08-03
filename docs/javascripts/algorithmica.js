/* Keep post metadata directly below the Markdown title, as in Algorithmica. */
function placePostMetadata() {
  const article = document.querySelector(".md-content--post .md-content__inner");
  const title = article && article.querySelector("h1");
  const metadata = article && article.querySelector(".algorithmica-post-meta");

  if (title && metadata && title.nextElementSibling !== metadata) {
    title.insertAdjacentElement("afterend", metadata);
  }
}

const sidebarPreferenceKey = "algorithmica-sidebar-collapsed";

function readSidebarPreference() {
  try {
    return window.localStorage.getItem(sidebarPreferenceKey) === "true";
  } catch (_) {
    return false;
  }
}

function writeSidebarPreference(collapsed) {
  try {
    window.localStorage.setItem(sidebarPreferenceKey, String(collapsed));
  } catch (_) {
    // The control still works when storage is unavailable.
  }
}

function setSidebarCollapsed(button, collapsed) {
  document.documentElement.classList.toggle("sidebar-is-collapsed", collapsed);
  button.setAttribute("aria-expanded", String(!collapsed));
  button.setAttribute(
    "aria-label",
    collapsed ? "Expand navigation sidebar" : "Collapse navigation sidebar"
  );
  button.title = collapsed ? "Expand sidebar" : "Collapse sidebar";
}

function setupSidebarCollapseButton() {
  const sidebar = document.querySelector(".md-sidebar--primary");
  if (!sidebar) return;

  let button = document.querySelector(".sidebar-collapse-toggle");
  if (!button) {
    button = document.createElement("button");
    button.type = "button";
    button.className = "sidebar-collapse-toggle";
    button.innerHTML = `
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <path d="M15.41 7.41 10.83 12l4.58 4.59L14 18l-6-6 6-6z"></path>
      </svg>`;
    document.body.appendChild(button);
    button.addEventListener("click", () => {
      const collapsed = !document.documentElement.classList.contains(
        "sidebar-is-collapsed"
      );
      setSidebarCollapsed(button, collapsed);
      writeSidebarPreference(collapsed);
    });
  }

  setSidebarCollapsed(button, readSidebarPreference());
}

if (typeof document$ !== "undefined") {
  document$.subscribe(() => {
    placePostMetadata();
    setupSidebarCollapseButton();
  });
} else {
  document.addEventListener("DOMContentLoaded", () => {
    placePostMetadata();
    setupSidebarCollapseButton();
  });
}
