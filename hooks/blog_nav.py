"""Populate the Blog navigation with the posts included in this build."""

from mkdocs.plugins import event_priority
from mkdocs.structure.nav import Link, Section


@event_priority(-100)
def on_nav(nav, *, config, files):
    blog = config.plugins["material/blog"].blog
    section = next(
        (item for item in nav.items if isinstance(item, Section) and item.title == "Blog"),
        None,
    )
    if section is None:
        return nav

    for post in blog.posts:
        link = Link(post.title, f"/{post.url}")
        link.parent = section
        section.children.append(link)

    return nav


def on_page_context(context, *, page, config, nav):
    """Keep the primary site navigation visible on individual blog posts."""
    if page.file.src_uri.startswith("blog/posts/"):
        hidden = page.meta.get("hide", [])
        page.meta["hide"] = [item for item in hidden if item != "navigation"]

    return context
