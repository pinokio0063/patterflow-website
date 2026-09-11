// Silent offline cache — no permission prompt. Files save on first visit.
if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
}

document.addEventListener('DOMContentLoaded', () => {

    // ----- DOM ELEMENTS -----
    const tableBody = document.getElementById('tableBody');
    const dataTable = document.getElementById('data-table');
    const cleanBtn = document.getElementById('cleanBtn'); 
    const bulkInput = document.getElementById('bulkInput');
    const themeToggle = document.getElementById('themeToggle');
    const settingsBtn = document.getElementById('settingsBtn');
    const settingsModal = document.getElementById('settingsModal');
    const closeSettings = document.querySelector('.close-settings');
    const jsonFileInput = document.getElementById('jsonFileInput');
    const zoomBox = document.getElementById('zoom-box');

    // T&C Elements
    const tcBtn = document.getElementById('tcBtn');
    const tcModal = document.getElementById('tcModal');
    const closeTc = document.querySelector('.close-tc');

    // Print Elements
    const printBtn = document.getElementById('printBtn');
    const printModal = document.getElementById('printModal');
    const closePrint = document.querySelector('.close-print');
    const startPrint = document.getElementById('startPrint');
    const jerseyImageInput = document.getElementById('jerseyImageInput');
    const printTitleInput = document.getElementById('printTitle');
    const fabricNameInput = document.getElementById('fabricName');
    const sleeveRibInput = document.getElementById('sleeveRib');
    const jerseyDescInput = document.getElementById('jerseyDesc');

    const INITIAL_ROWS = 100;
    const COL = { SL: 0, NAME: 1, NUM: 2, SIZE: 3, FULL: 4, HALF: 5, PANT: 6 };
    const VALID_SIZES = ["2Y", "4Y", "6Y", "8Y", "10Y", "12Y", "XS", "S", "M", "L", "XL", "XXL", "XXXL", "2XL", "3XL", "4XL", "5XL", "6XL", "7XL", "8XL"];
    const SIZE_MAP = { 'XXL': '2XL', 'XXXL': '3XL', 'XXXXL': '4XL', 'XXXXXL': '5XL' };

    let startCell = null;
    let currentZoom = 1.0;
    let customColCount = 0; 

    function getColLetter(index) {
        let letter = "";
        let temp = index;
        while (temp >= 0) {
            letter = String.fromCharCode((temp % 26) + 65) + letter;
            temp = Math.floor(temp / 26) - 1;
        }
        return letter;
    }

    addRows(INITIAL_ROWS);

    // ----- MODALS & THEME -----
    settingsBtn.onclick = () => settingsModal.style.display = "block";
    closeSettings.onclick = () => settingsModal.style.display = "none";
    
    tcBtn.onclick = () => {
        tcModal.style.display = "block";
    };
    closeTc.onclick = () => tcModal.style.display = "none";

    printBtn.onclick = () => printModal.style.display = "block";
    closePrint.onclick = () => printModal.style.display = "none";

    window.onclick = (e) => {
        if (e.target == settingsModal) settingsModal.style.display = "none";
        if (e.target == printModal) printModal.style.display = "none";
        if (e.target == tcModal) tcModal.style.display = "none";
    };

    themeToggle.onclick = () => {
        document.body.classList.toggle('dark-mode');
        const isDark = document.body.classList.contains('dark-mode');
        localStorage.setItem('pf_theme', isDark ? 'dark' : 'light');
    };
    if (localStorage.getItem('pf_theme') === 'dark') document.body.classList.add('dark-mode');

    function addRows(count) {
        const start = tableBody.rows.length;
        const fragment = document.createDocumentFragment();
        for (let i = 0; i < count; i++) {
            const row = document.createElement('tr');
            
            let rowHTML = `
                <td class="sl-col">${start + i + 1}</td>
                <td contenteditable="true"></td>
                <td contenteditable="true"></td>
                <td contenteditable="true"></td>
                <td class="clickable-cell" data-type="full"></td>
                <td class="clickable-cell" data-type="half"></td>
                <td contenteditable="true"></td>
            `;
            
            for (let j = 0; j < customColCount; j++) {
                rowHTML += `<td contenteditable="true"></td>`;
            }
            
            rowHTML += `<td contenteditable="true"></td>`;
            
            row.innerHTML = rowHTML;
            fragment.appendChild(row);
        }
        tableBody.appendChild(fragment);
    }

    // Clean All Data
    cleanBtn.onclick = () => {
        if (!confirm("Are you sure you want to clear all data and remove custom columns?")) return;
        
        customColCount = 0;
        
        const headerRow = dataTable.rows[0];
        const custThs = headerRow.querySelectorAll('th');
        custThs.forEach(th => {
            if (th.innerText.startsWith('CUST-')) th.remove();
        });
        
        tableBody.innerHTML = "";
        addRows(INITIAL_ROWS);
    };

    function applyFormula(cell) {
        const text = cell.innerText.trim();
        const match = text.match(/^(\d+)\s*\*\s*(.+)$/);
        if (match) {
            const count = parseInt(match[1]);
            const value = match[2].toUpperCase();
            const startR = cell.parentElement.rowIndex;
            for (let i = 0; i < count; i++) {
                const targetRow = dataTable.rows[startR + i];
                if (targetRow) {
                    targetRow.cells[cell.cellIndex].innerText = SIZE_MAP[value] || value;
                    validateRow(targetRow); 
                }
            }
        } else {
            cell.innerText = SIZE_MAP[text.toUpperCase()] || text.toUpperCase();
            validateRow(cell.parentElement); 
        }
    }

    // SIZE এবং PANT-SIZE ভ্যালিডেশন
    function validateRow(row) {
        if (!row || row.rowIndex === 0) return; 
        const sizeCell = row.cells[COL.SIZE];
        const pantCell = row.cells[COL.PANT];
        if (!sizeCell || !pantCell) return;

        const sizeVal = sizeCell.innerText.trim().toUpperCase();
        const pantVal = pantCell.innerText.trim().toUpperCase();

        if (sizeVal !== "" && !VALID_SIZES.includes(sizeVal)) {
            sizeCell.classList.add('invalid-size-cell');
        } else {
            sizeCell.classList.remove('invalid-size-cell');
        }

        if (pantVal !== "" && !VALID_SIZES.includes(pantVal)) {
            pantCell.classList.add('invalid-size-cell');
        } else {
            pantCell.classList.remove('invalid-size-cell');
        }
    }
    
    // ----- SMART EXCEL-STYLE PASTE -----
    tableBody.addEventListener('paste', (e) => {
        const targetCell = e.target.closest('td');
        if (!targetCell || targetCell.contentEditable !== "true") return;

        e.preventDefault();
        const clipboardData = (e.clipboardData || window.clipboardData).getData('text');
        const rows = clipboardData.split(/\r\n|\n|\r/);
        
        const startRowIdx = targetCell.parentElement.rowIndex - 1;
        const startColIdx = targetCell.cellIndex;

        rows.forEach((rowText, rOffset) => {
            if (rowText.trim() === "" && rOffset !== 0) return;
            const columns = rowText.split('\t'); 
            const targetRow = tableBody.rows[startRowIdx + rOffset];
            
            if (targetRow) {
                columns.forEach((colText, cOffset) => {
                    const finalCell = targetRow.cells[startColIdx + cOffset];
                    if (finalCell && finalCell.contentEditable === "true") {
                        finalCell.innerText = colText.trim();
                        if (finalCell.cellIndex === COL.SIZE || finalCell.cellIndex === COL.PANT) {
                            applyFormula(finalCell);
                        }
                    }
                });
            }
        });
    });

    // ----- SMART QUICK IMPORT -----
    document.getElementById('processBulkBtn').onclick = () => {
        const rawText = bulkInput.value.trim();
        if (!rawText) return alert("Please paste data first!");
        const lines = rawText.split('\n');
        let importedCount = 0;
        lines.forEach(line => {
            let cleanLine = line.replace(/^[0-9]+[\.\)\s\-]+/, "").replace(/[^a-zA-Z0-9\s]/g, " ");
            let words = cleanLine.trim().split(/\s+/);
            let name = "", number = "", size = "", sleeve = "";
            
            words = words.filter(word => {
                let w = word.toLowerCase();
                if (w === "half" || w === "haf") { sleeve = "HALF"; return false; }
                if (w === "full" || w === "ful") { sleeve = "FULL"; return false; }
                return true;
            });

            words = words.filter(word => {
                let w = word.toUpperCase();
                if (VALID_SIZES.includes(w) || SIZE_MAP[w]) { size = SIZE_MAP[w] || w; return false; }
                return true;
            });
            let numFound = false;
            words = words.filter(word => {
                if (!numFound && /^\d+$/.test(word)) { number = word; numFound = true; return false; }
                return true;
            });
            name = words.join(" ").trim();
            if (name || number || size) {
                const row = [...tableBody.rows].find(r => !r.cells[COL.NAME].innerText && !r.cells[COL.NUM].innerText);
                const targetRow = row || (addRows(1), tableBody.lastElementChild);
                targetRow.cells[COL.NAME].innerText = name;
                targetRow.cells[COL.NUM].innerText = number;
                targetRow.cells[COL.SIZE].innerText = size;
                if (sleeve) {
                    targetRow.cells[COL.FULL].innerText = (sleeve === "FULL" ? "FULL" : "");
                    targetRow.cells[COL.HALF].innerText = (sleeve === "HALF" ? "HALF" : ""); // COL.HAF সংশোধন করে COL.HALF করা হয়েছে
                }
                validateRow(targetRow); 
                importedCount++;
            }
        });
        if (importedCount > 0) { alert(importedCount + " entries loaded!"); bulkInput.value = ""; }
    };

    // ----- SELECTION & KEYBOARD -----
    tableBody.addEventListener('mousedown', (e) => {
        const cell = e.target.closest('td');
        if (!cell || cell.classList.contains('sl-col')) return;
        if (e.shiftKey && startCell) {
            e.preventDefault();
            const r1 = startCell.parentElement.rowIndex, c1 = startCell.cellIndex;
            const r2 = cell.parentElement.rowIndex, c2 = cell.cellIndex;
            document.querySelectorAll('td.selected').forEach(td => td.classList.remove('selected'));
            for (let i = Math.min(r1, r2); i <= Math.max(r1, r2); i++) {
                for (let j = Math.min(c1, c2); j <= Math.max(c1, c2); j++) dataTable.rows[i].cells[j].classList.add('selected');
            }
        } else {
            document.querySelectorAll('td.selected').forEach(td => td.classList.remove('selected'));
            startCell = cell; cell.classList.add('selected');
        }
    });

    document.addEventListener('keydown', (e) => {
        const selected = document.querySelectorAll('td.selected');
        if (selected.length === 0) return;
        if (e.key === 'Delete') {
            selected.forEach(td => { 
                if (td.contentEditable === "true" || td.classList.contains('clickable-cell')) {
                    td.innerText = ""; 
                    td.classList.remove('invalid-size-cell'); 
                }
            });
        }
        if (e.key === 'Enter') {
            const first = selected[0];
            if (first.cellIndex === COL.FULL || first.cellIndex === COL.HALF) {
                e.preventDefault();
                const type = first.cellIndex === COL.FULL ? "FULL" : "HALF";
                selected.forEach(td => {
                    const r = td.parentElement;
                    if (td.cellIndex === COL.FULL || td.cellIndex === COL.HALF) {
                        r.cells[COL.FULL].innerText = ""; r.cells[COL.HALF].innerText = "";
                        r.cells[first.cellIndex].innerText = type;
                    }
                });
            } else if (first.cellIndex === COL.SIZE || first.cellIndex === COL.PANT) {
                e.preventDefault(); applyFormula(first); first.blur();
            }
        }
    });

    tableBody.addEventListener('blur', (e) => {
        if (e.target.cellIndex === COL.SIZE || e.target.cellIndex === COL.PANT) applyFormula(e.target);
    }, true);

    tableBody.addEventListener('click', (e) => {
        if (e.target.classList.contains('clickable-cell')) {
            const r = e.target.parentElement;
            const set = e.target.innerText !== "";
            r.cells[COL.FULL].innerText = ""; r.cells[COL.HALF].innerText = "";
            if (!set) e.target.innerText = e.target.dataset.type === 'full' ? 'FULL' : 'HALF';
        }
    });
    
    const toBase64 = file => new Promise((resolve, reject) => {
        const reader = new FileReader(); 
        reader.readAsDataURL(file);
        reader.onload = () => resolve(reader.result); 
        reader.onerror = e => reject(e);
    });

    // ----- PDF PRINT GENERATION -----
    startPrint.onclick = async () => {
        try {
            const title = printTitleInput.value || "Data Entry Sheet";
            const fabric = fabricNameInput.value || "N/A";
            const rib = sleeveRibInput.value;
            const desc = jerseyDescInput.value || "No special instructions.";

            const tableData = [...tableBody.rows].map(r => {
                const fullVal = r.cells[COL.FULL] ? r.cells[COL.FULL].innerText.trim() : "";
                const halfVal = r.cells[COL.HALF] ? r.cells[COL.HALF].innerText.trim() : "";
                return {
                    sl: r.cells[COL.SL].innerText, 
                    name: r.cells[COL.NAME].innerText, 
                    num: r.cells[COL.NUM].innerText,
                    sz: r.cells[COL.SIZE].innerText, 
                    slv: fullVal || halfVal || ""
                };
            }).filter(d => d.name.trim() || d.num.trim() || d.sz.trim());

            if (tableData.length === 0) {
                alert("No data found in table! Please enter some data first.");
                return;
            }

            let jerseyImg = "";
            if (jerseyImageInput.files[0]) {
                jerseyImg = await toBase64(jerseyImageInput.files[0]);
            }

            const element = document.createElement('div');
            element.style.padding = "10mm"; 
            element.style.background = "white"; 
            element.style.color = "black";
            element.style.width = "210mm"; 

            element.innerHTML = `
                <div style="display:flex; justify-content:space-between; align-items:flex-end; border-bottom:2px solid #000; padding-bottom:5px; margin-bottom:10px; font-family: sans-serif;">
                    <div>
                        <h1 style="font-size:22px; margin:0; color: #000;">${title}</h1>
                        <p style="font-size:10px; margin:2px 0;">Generated by PatternFlow Pro | Date: ${new Date().toLocaleDateString()}</p>
                    </div>
                    <div style="font-size:14px; font-weight:bold; color:#0078d4;">PatternFlow Pro</div>
                </div>
                <div style="display:flex; gap:10px; font-family: sans-serif;">
                    <div style="flex:1;">
                        <table style="width:100%; border-collapse:collapse; border: 1px solid #000;">
                            <thead>
                                <tr style="background:#f0f0f0;">
                                    <th style="border:1px solid #000; padding:5px; font-size:10px;">SL</th>
                                    <th style="border:1px solid #000; padding:5px; font-size:10px; text-align:left;">NAME</th>
                                    <th style="border:1px solid #000; padding:5px; font-size:10px;">NUM</th>
                                    <th style="border:1px solid #000; padding:5px; font-size:10px;">SIZE</th>
                                    <th style="border:1px solid #000; padding:5px; font-size:10px;">SLV</th>
                                </tr>
                            </thead>
                            <tbody>
                                ${tableData.map(d => `
                                    <tr>
                                        <td style="border:1px solid #000; padding:4px; font-size:10px; text-align:center;">${d.sl}</td>
                                        <td style="border:1px solid #000; padding:4px; font-size:10px; font-weight:bold;">${d.name}</td>
                                        <td style="border:1px solid #000; padding:4px; font-size:10px; text-align:center;">${d.num}</td>
                                        <td style="border:1px solid #000; padding:4px; font-size:10px; text-align:center;">${d.sz}</td>
                                        <td style="border:1px solid #000; padding:4px; font-size:10px; text-align:center;">${d.slv}</td>
                                    </tr>
                                `).join('')}
                            </tbody>
                        </table>
                    </div>
                    <div style="width:70mm; border:1px solid #000; padding:10px; background: #fff;">
                        <div style="text-align:center; border-bottom:1px solid #000; padding-bottom:10px; margin-bottom:10px;">
                            ${jerseyImg ? `<img src="${jerseyImg}" style="width:100%; max-height:250px; object-fit:contain;">` : `<div style="height:120px; background:#f0f0f0; display:flex; align-items:center; justify-content:center; font-size:12px; color:#666; border: 1px dashed #ccc;">Jersey Image Preview</div>`}
                        </div>
                        <div style="font-size:12px; line-height: 1.6;">
                            <p><strong>Fabric:</strong> ${fabric}</p>
                            <p><strong>Sleeve Rib:</strong> ${rib}</p>
                            <div style="margin-top:10px; border-top:1px dashed #000; padding-top:5px;">
                                <strong>Notes:</strong><br>
                                <span style="font-size:11px;">${desc}</span>
                            </div>
                        </div>
                    </div>
                </div>
            `;

            const opt = {
                margin: [5, 5, 5, 5],
                filename: `${title.replace(/\s+/g, '_')}.pdf`,
                image: { type: 'jpeg', quality: 0.98 },
                html2canvas: { scale: 2, useCORS: true, logging: false },
                jsPDF: { unit: 'mm', format: 'a4', orientation: 'portrait' }
            };

            await html2pdf().set(opt).from(element).save();
            printModal.style.display = "none";

        } catch (error) {
            console.error("PDF Generation Error:", error);
            alert("Something went wrong while generating PDF! Check console for details.");
        }
    };

    // ----- ZOOM & FILE OPS -----
    document.getElementById('zoomInBtn').onclick = () => { currentZoom += 0.1; applyZ(); };
    document.getElementById('zoomOutBtn').onclick = () => { currentZoom -= 0.1; applyZ(); };
    function applyZ() { currentZoom = Math.max(0.4, Math.min(currentZoom, 1.5)); zoomBox.style.transform = `scale(${currentZoom})`; document.getElementById('zoomLevelDisplayText').innerText = Math.round(currentZoom * 100) + "%"; }
    document.getElementById('addRowBtn').onclick = () => addRows(50);



    // ডায়নামিক কলাম যুক্ত করার ইভেন্ট (সবুজ লম্বা বাটন)
    document.getElementById('addColBtn').onclick = () => {
        const targetColIdx = dataTable.rows[0].cells.length - 1;
        const letter = getColLetter(customColCount);

        const th = document.createElement('th');
        th.textContent = `CUST-${letter}`; // পরিবর্তন: এখানে শুধু CUST-A নাম থাকবে
        dataTable.rows[0].insertBefore(th, dataTable.rows[0].cells[targetColIdx]);


        [...tableBody.rows].forEach(row => {
            const td = document.createElement('td');
            td.contentEditable = "true";
            row.insertBefore(td, row.cells[targetColIdx]);
        });

        customColCount++;
    };

    // Save JSON
    document.getElementById('saveJsonBtn').onclick = () => {
        // th.childNodes[0] ব্যবহার করে মূল কলাম হেডার (যেমন "CUST-A") ফিল্টার করা হয়েছে
        const headers = [...dataTable.rows[0].cells].map(th => th.childNodes[0].textContent.trim());
        const data = [...tableBody.rows].map(r => {
            let rowData = {};
            headers.forEach((header, colIdx) => {
                if (header === "SL") return;
                if (header === "FULL SLV" || header === "HALF SLV") {
                    rowData["SLV"] = r.cells[colIdx].innerText.trim() || rowData["SLV"] || "";
                    return;
                }
                rowData[header] = r.cells[colIdx].innerText.trim();
            });
            return rowData;
        }).filter(d => d.NAME || d.NUMBER || d.SIZE);

        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = `PF_Data_${new Date().toLocaleDateString()}.json`; a.click();
    };

    // Open JSON
    document.getElementById('openJsonBtn').onclick = () => jsonFileInput.click();
    jsonFileInput.onchange = (e) => {
        if (!e.target.files[0]) return;
        const reader = new FileReader();
        reader.onload = (ev) => {
            const data = JSON.parse(ev.target.result);
            if (data.length === 0) return;

            customColCount = 0;
            const headerRow = dataTable.rows[0];
            const existingThs = headerRow.querySelectorAll('th');
            existingThs.forEach(th => {
                if (th.innerText.startsWith('CUST-')) th.remove();
            });

            let custKeys = new Set();
            data.forEach(item => {
                Object.keys(item).forEach(key => {
                    if (key.startsWith('CUST-')) custKeys.add(key);
                });
            });
            const sortedCustKeys = [...custKeys].sort();
            customColCount = sortedCustKeys.length;


            const commentsTh = headerRow.cells[headerRow.cells.length - 1];
            sortedCustKeys.forEach((key, j) => {
                const th = document.createElement('th');
                th.textContent = key; // পরিবর্তন: এখানে শুধু CUST-A নাম থাকবে
                headerRow.insertBefore(th, commentsTh);
            });
            

            tableBody.innerHTML = "";
            data.forEach((d, i) => {
                const row = document.createElement('tr');
                
                let rowHTML = `
                    <td class="sl-col">${i+1}</td>
                    <td contenteditable="true">${d.NAME || ""}</td>
                    <td contenteditable="true">${d.NUMBER || ""}</td>
                    <td contenteditable="true">${d.SIZE || ""}</td>
                    <td class="clickable-cell" data-type="full">${d.SLV === "FULL" ? "FULL" : ""}</td>
                    <td class="clickable-cell" data-type="half">${d.SLV === "HALF" ? "HALF" : ""}</td>
                    <td contenteditable="true">${d["PANT-SIZE"] || ""}</td>
                `;
                
                sortedCustKeys.forEach(key => {
                    rowHTML += `<td contenteditable="true">${d[key] || ""}</td>`;
                });
                
                rowHTML += `<td contenteditable="true">${d.COMMENTS || ""}</td>`;
                
                row.innerHTML = rowHTML;
                tableBody.appendChild(row);
                validateRow(row); 
            });

            if (tableBody.rows.length < INITIAL_ROWS) addRows(INITIAL_ROWS - tableBody.rows.length);
        };
        reader.readAsText(e.target.files[0]);
    };
    
});