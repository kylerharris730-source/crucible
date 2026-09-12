/* ===========================================================================
   search.js -- filters the generated page index.

   Hand-written, like wiki.css and wiki.js: this is behaviour, and it holds no
   facts about the game. The facts are in searchdata.js, which the generator
   writes from the pages it actually produced.

   Deliberately simple. A substring match over a few hundred titles is instant
   and, more usefully, it is PREDICTABLE -- a fuzzy matcher that silently
   reorders results is worse than no search on a reference site, because the
   reader cannot tell whether the thing they wanted is absent or merely ranked
   fourteenth.

   The one piece of cleverness is the ordering below, and it is the obvious
   one: a title that starts with what you typed comes before one that merely
   contains it, so "iron" finds Iron before Iron Ore before Molten Iron.
   ========================================================================= */
(function () {
    "use strict";

    var box = document.getElementById("q");
    var list = document.getElementById("results");
    var count = document.getElementById("n");
    var pages = window.WIKI_PAGES || [];

    if (!box || !list) return;

    /* Section names double as a label on each result, so "Torch" the material
       and "Torch" the device are told apart without opening either. */
    function render(rows, needle) {
        list.innerHTML = "";
        for (var i = 0; i < rows.length; ++i) {
            var li = document.createElement("li");
            var a = document.createElement("a");
            a.href = rows[i][1];
            a.textContent = rows[i][0];
            li.appendChild(a);
            if (rows[i][2]) {
                var tag = document.createElement("span");
                tag.className = "dim";
                tag.textContent = " — " + rows[i][2];
                li.appendChild(tag);
            }
            list.appendChild(li);
        }
        if (count) {
            count.textContent = !needle
                ? pages.length + " pages"
                : rows.length + " of " + pages.length;
        }
    }

    function search() {
        var needle = box.value.toLowerCase().trim();
        if (!needle) { render([], ""); return; }

        var starts = [], contains = [];
        for (var i = 0; i < pages.length; ++i) {
            var title = pages[i][0].toLowerCase();
            var at = title.indexOf(needle);
            if (at === 0) starts.push(pages[i]);
            else if (at > 0) contains.push(pages[i]);
        }
        render(starts.concat(contains), needle);
    }

    box.addEventListener("input", search);

    /* Enter on a single result goes straight there -- the case where you
       already know the name and only want the page. */
    box.addEventListener("keydown", function (e) {
        if (e.key !== "Enter") return;
        var first = list.querySelector("a");
        if (first) window.location.href = first.getAttribute("href");
    });

    /* A ?q= in the URL pre-fills the box, so a search is a link you can share
       or bookmark. */
    var q = /[?&]q=([^&]*)/.exec(window.location.search);
    if (q) box.value = decodeURIComponent(q[1].replace(/\+/g, " "));

    search();
}());
