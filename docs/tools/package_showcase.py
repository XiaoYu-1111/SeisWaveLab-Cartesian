"""Compatibility entry point for the current offline/Hexo publication package."""
from package_documentation import package_documentation


def package_showcase() -> None:
    """Regenerate the maintained article and complete current publication bundle."""
    package_documentation()


if __name__ == "__main__":
    package_showcase()
