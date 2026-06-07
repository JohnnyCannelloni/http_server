// Fetch the fake status endpoint and display it on the landing page.
// This exists primarily to demonstrate that the server serves
// application/javascript and application/json correctly.

(function() {
    var el = document.getElementById("status");
    if (!el) return;

    fetch("/api/status.json")
        .then(function(res) {
            if (!res.ok) throw new Error("HTTP " + res.status);
            return res.json();
        })
        .then(function(data) {
            el.textContent = JSON.stringify(data, null, 2);
        })
        .catch(function(err) {
            el.textContent = "failed to load status: " + err.message;
        });
})();
