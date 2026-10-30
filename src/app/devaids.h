// Development aids driven by environment variables, for checking the UI
// and the bench without a screenshot tool: SUPERFAR_BENCH_THEN navigates
// after the first load, SUPERFAR_DEV_KEYS posts keys, SUPERFAR_GRAB
// renders the window to a file and quits.
#pragma once

class MainWindow;

void installDevAids(MainWindow* window);
