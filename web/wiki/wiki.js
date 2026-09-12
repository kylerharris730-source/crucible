/* ===========================================================================
   wiki.js -- sorting and filtering for the index tables.

   HAND-WRITTEN, like wiki.css and for the same reason: this is behaviour, not
   facts. It holds nothing about the game and so cannot drift from it.

   It is also deliberately the ONLY script on the site, and it is an
   enhancement rather than a requirement. Every table ships complete in the
   HTML -- all 116 rows of it -- so with JavaScript off the page is still a full
   reference you can read and use the browser's own find on. There is no JSON
   to fetch, which means there is no second copy of the data to get out of sync
   with the pages.

   Progressive enhancement here is not a principle being observed for its own
   sake. A wiki is something people reach from a search result on a bad
   connection, and a page that renders nothing until a script has run is a page
   that sometimes renders nothing.
   ========================================================================= */
(function () {
    "use strict";

    /* Sorting reads a data-sort attribute when the cell has one and the visible
       text otherwise. Numeric columns carry it because "12" and "112" sort
       wrongly as text, and because a blank cell has to sort as "absent" rather
       than as zero -- a material with no ignition point does not ignite at
       0 degrees, it does not ignite. */
    function cellValue(row, index) {
        var cell = row.children[index];
        if (!cell) return "";
        var explicit = cell.getAttribute("data-sort");
        return explicit !== null ? explicit : cell.textContent.trim();
    }

    function compare(a, b, index, numeric) {
        var x = cellValue(a, index), y = cellValue(b, index);
        if (numeric) {
            /* Empty sorts last in both directions: "no value" is not a small
               value, and burying the absent rows at one end is what makes
               sorting by ignition point actually answer "what burns?". */
            var ex = x === "", ey = y === "";
            if (ex && ey) return 0;
            if (ex) return 1;
            if (ey) return -1;
            return parseFloat(x) - parseFloat(y);
        }
        return x.localeCompare(y);
    }

    function makeSortable(table) {
        var headers = table.querySelectorAll("thead th.sortable");
        Array.prototype.forEach.call(headers, function (th, i) {
            var index = th.cellIndex;
            var numeric = th.classList.contains("num");
            th.addEventListener("click", function () {
                var body = table.tBodies[0];
                var rows = Array.prototype.slice.call(body.rows);
                var descending = th.classList.contains("asc");

                rows.sort(function (a, b) {
                    var r = compare(a, b, index, numeric);
                    return descending ? -r : r;
                });

                Array.prototype.forEach.call(headers, function (other) {
                    other.classList.remove("asc", "desc");
                });
                th.classList.add(descending ? "desc" : "asc");

                /* Re-append in order. The rows are existing nodes, so this
                   moves them rather than rebuilding the table -- which keeps
                   any open selection and costs nothing at 116 rows. */
                rows.forEach(function (row) { body.appendChild(row); });
            });
            void i;
        });
    }

    /* The filter matches against a row's data-search attribute, which the
       generator fills with everything worth matching on -- name and kind, not
       just what is visible in the first column. Typing "gas" should find the
       gases. */
    function makeFilterable(table) {
        var wrap = table.closest(".tablewrap");
        var bar = wrap && wrap.previousElementSibling;
        if (!bar || !bar.classList.contains("filterbar")) return;
        var input = bar.querySelector("input");
        var count = bar.querySelector(".count");
        if (!input) return;

        var total = table.tBodies[0].rows.length;
        var chips = bar.querySelectorAll(".chip");
        var activeChip = "";

        function apply() {
            var needle = input.value.toLowerCase().trim();
            var rows = table.tBodies[0].rows;
            var shown = 0;
            for (var i = 0; i < rows.length; ++i) {
                var row = rows[i];
                var hay = (row.getAttribute("data-search") || "").toLowerCase();
                var kind = row.getAttribute("data-kind") || "";
                var ok = (!needle || hay.indexOf(needle) >= 0) &&
                         (!activeChip || kind === activeChip);
                row.hidden = !ok;
                if (ok) ++shown;
            }
            if (count) {
                count.textContent = shown === total
                    ? total + " of " + total
                    : shown + " of " + total;
            }
        }

        input.addEventListener("input", apply);
        Array.prototype.forEach.call(chips, function (chip) {
            chip.addEventListener("click", function () {
                var want = chip.getAttribute("data-kind") || "";
                /* Clicking the active chip clears it, so the filter can always
                   be undone without hunting for an "all" button. */
                activeChip = (activeChip === want) ? "" : want;
                Array.prototype.forEach.call(chips, function (other) {
                    other.classList.toggle(
                        "on", other.getAttribute("data-kind") === activeChip);
                });
                apply();
            });
        });

        apply();
    }

    function ready() {
        var tables = document.querySelectorAll("table.index");
        Array.prototype.forEach.call(tables, function (table) {
            makeSortable(table);
            makeFilterable(table);
        });
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", ready);
    } else {
        ready();
    }
}());
