/* Dreamcast Linux landing — tiny progressive enhancements, no dependencies. */
(function () {
  "use strict";

  // Click-to-play videos: each poster button swaps itself for a real <video>
  // only when clicked, so nothing downloads until the visitor asks for it.
  document.querySelectorAll(".video[data-src]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var v = document.createElement("video");
      v.src = btn.getAttribute("data-src");
      v.className = "video__el";
      v.controls = true;
      v.autoplay = true;
      v.playsInline = true;
      v.setAttribute("playsinline", "");
      btn.replaceWith(v);
      v.focus();
    });
  });

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
