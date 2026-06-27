/* Dreamcast Linux landing — tiny progressive enhancements, no dependencies. */
(function () {
  "use strict";

  // Gallery lightbox: click a photo to view it full-size in an overlay.
  var lb = document.getElementById("lightbox");
  if (lb) {
    var lbImg = lb.querySelector(".lightbox__img");
    var lbCap = lb.querySelector(".lightbox__cap");
    var lastFocus = null;

    var openLightbox = function (full, alt, caption) {
      lbImg.src = full;
      lbImg.alt = alt || "";
      lbCap.textContent = caption || "";
      lb.hidden = false;
    };
    var closeLightbox = function () {
      lb.hidden = true;
      lbImg.src = "";
      if (lastFocus) lastFocus.focus();
    };

    document.querySelectorAll(".shot__btn[data-full]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        lastFocus = btn;
        var img = btn.querySelector("img");
        var cap = btn.closest(".shot").querySelector("figcaption");
        openLightbox(
          btn.getAttribute("data-full"),
          img ? img.alt : "",
          cap ? cap.textContent : ""
        );
        lb.querySelector(".lightbox__close").focus();
      });
    });

    lb.addEventListener("click", function (e) {
      // close when clicking the backdrop or the close button (not the image)
      if (e.target === lb || e.target.classList.contains("lightbox__close")) closeLightbox();
    });
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape" && !lb.hidden) closeLightbox();
    });
  }

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
