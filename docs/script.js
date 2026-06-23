/* Dreamcast Linux landing — tiny progressive enhancements, no dependencies. */
(function () {
  "use strict";

  // Reveal sections on scroll (skipped if user prefers reduced motion).
  var reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (reduce || !("IntersectionObserver" in window)) return;

  var targets = document.querySelectorAll(".card, .step, .tribute, .stat");
  targets.forEach(function (el) {
    el.style.opacity = "0";
    el.style.transform = "translateY(16px)";
    el.style.transition = "opacity .5s ease, transform .5s ease";
  });

  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (!entry.isIntersecting) return;
      var el = entry.target;
      el.style.opacity = "1";
      el.style.transform = "none";
      io.unobserve(el);
    });
  }, { threshold: 0.12 });

  targets.forEach(function (el) { io.observe(el); });
})();
