const fs = require('fs');

// The following object will contain all documented functions and constants. It requires the lcdocs_summary.json:
//{
// 	"constants": [
// 		{
// 			"name": "NICE_CONSTANT"
// 		}
//  ],
//	"functions": [
// 		{
// 			"name": "NiceFunction"
// 		}
//  ]
//}
const summaryReportDocs = JSON.parse(fs.readFileSync('.github/workflows/C4ScriptDocAnalyzer/lcdocs_summary.json').toString());
console.log(`Loaded generated file with C4Script functions and constants documented in the current lcdocs master https://github.com/legacyclonk/lcdocs`);

// The following array will contain all C4Script functions defined in C4Script.cpp.
const c4ScriptFunctions = readAndFilterFile('./src/C4Script.cpp', [
    {
        regex: /AddFunc\(pEngine, +"(\w+)", +Fn\w+(, +(false|true))?\);/gm,
        captureGroup: 1,
    },
    {
        regex: /new\sC4Aul\w+<\w+,\s?\w+>\s*{pEngine,\s"(\w+)"/gm,
        captureGroup: 1,
    }
]);
console.log(`Loaded C4Script.cpp functions`);

// The following array will contain all C4Script constants defined in C4Script.cpp.
const c4ScriptConstants = readAndFilterFile('./src/C4Script.cpp', [
    {
        regex: /^\t{\s?"(\w+)",\s*\w+,\s*[\w<>():]+\s*}(,|\r?\n};)(\s*\/\/(\s*[\w()/-;,]+)+)?/gm,
        captureGroup: 1,
    }
]);
console.log(`Loaded C4Script.cpp constants`);


// Searching the whole directory for files of filetype ".c".
// All matching files are read in and filtered for function names in the ".c"-files that are not excluded with "// internal" in the line before the function declaration.
const systemC4gDirectory = 'planet/System.c4g/';
const systemC4gFileNames = fs.readdirSync(systemC4gDirectory, {
    "encoding": "ascii",
    "withFileTypes": false
});
const excludedFiles = [
    'C4.c' // Old functions of LC should no longer be used.
];
const cFileNames = systemC4gFileNames.filter(__filename => __filename.endsWith('.c')).filter(__filename => !excludedFiles.includes(__filename));
const cFunctions = cFileNames.map(
    __filename => removeOverloadingFunctions(
        readAndFilterFile(systemC4gDirectory.concat(__filename),[
    {
        // "//internal" in the line before the function excludes the function.
        // commented out functions are also excluded (//global func...)
        regex: /(?<!\/\/\s?internal\r?\n)(?<!\/\/\s?)global func (\w*)\s?\(/gm,
        captureGroup: 1,
    }
    ]), c4ScriptFunctions)
);
console.log(`Loaded helper files from System.c4g`);

// Adding functions from C4Script.cpp to functions from .c-files in System.c4g.
// This array of arrays will be searched for negative comparison results with the lcdocs.
const engineFunctions = [c4ScriptFunctions].concat(cFunctions);
const engineFileNames = ['C4Script.cpp'].concat(cFileNames);

// Main magic here: Performing the actual search.
const summaryReportFunctions = findEntities(engineFunctions, summaryReportDocs.functions.map(func => func.name), '()', 'functions');
const summaryReportConstants = findEntities([c4ScriptConstants], summaryReportDocs.constants.map(constant => constant.name), '', 'constants');

// Print statistics and results
let colorUndocumentedFunc = summaryReportFunctions.get('definedOnly').length === 0 ? '\x1b[42m\x1b[30m' : '\x1b[41m\x1b[30m';
let colorUndocumentedConst = summaryReportConstants.get('definedOnly').length === 0 ? '\x1b[42m\x1b[30m' : '\x1b[41m\x1b[30m';
let colorUndefinedFunc = summaryReportFunctions.get('documentedOnly').length === 0 ? '\x1b[42m\x1b[30m' : '\x1b[43m\x1b[30m';
let colorUndefinedConst = summaryReportConstants.get('documentedOnly').length === 0 ? '\x1b[42m\x1b[30m' : '\x1b[43m\x1b[30m';
console.log(`\n * ${colorUndocumentedFunc}${summaryReportFunctions.get('definedOnly').length} functions are undocumented.\x1b[0m`);
console.log(` * ${colorUndocumentedConst}${summaryReportConstants.get('definedOnly').length} constants are undocumented.\x1b[0m`);
console.log(` * ${colorUndefinedFunc}${summaryReportFunctions.get('documentedOnly').length} functions are documented but undefined in the engine.\x1b[0m`);
console.log(` * ${colorUndefinedConst}${summaryReportConstants.get('documentedOnly').length} constants are documented but undefined in the engine.\x1b[0m`);
console.log(` * ${summaryReportFunctions.get('definedAndDocumented').length} functions and ${summaryReportConstants.get('definedAndDocumented').length} constants are defined and documented.`);
console.log(`\nStatistics: There are
 * ${c4ScriptFunctions.length} defined functions in C4Script.cpp,
 * ${c4ScriptConstants.length} defined constants in C4Script.cpp,
 * ${cFunctions.flat().length} defined functions in System.c4g and
 * ${summaryReportDocs.functions.length} documented functions in current lcdocs master.
 * ${summaryReportDocs.constants.length} documented constants in current lcdocs master.\n`);

// Integrity check
const sumEntitiesInEngine =
    summaryReportFunctions.get('definedOnly').length +
    summaryReportFunctions.get('definedAndDocumented').length +
    summaryReportConstants.get('definedOnly').length +
    summaryReportConstants.get('definedAndDocumented').length;
const sumEntitiesInEngineOnly =
    summaryReportFunctions.get('definedOnly').length +
    summaryReportConstants.get('definedOnly').length;
const sumEntitiesInDocs = summaryReportDocs.functions.length + summaryReportDocs.constants.length;
const sumEntitiesInDocsOnly =
    summaryReportFunctions.get('documentedOnly').length +
    summaryReportConstants.get('documentedOnly').length;

if (sumEntitiesInEngine - sumEntitiesInEngineOnly === sumEntitiesInDocs - sumEntitiesInDocsOnly) {
    console.log(`All entities processed, exiting.`);
    process.exit();
} else {
    console.log(`\x1b[41m\x1b[30mERROR: The set of parsed functions is not distinct!\nWe have ${sumEntitiesInEngine} entities in the engine where ${sumEntitiesInEngineOnly} are only in the engine defined so intersection set is ${sumEntitiesInEngine - sumEntitiesInEngineOnly}. On the other hand we have ${sumEntitiesInDocs} entities in the docs where ${sumEntitiesInDocsOnly} are in the docs only and not defined in the engine so the intersection set is ${sumEntitiesInDocs - sumEntitiesInDocsOnly}. From this it can be concluded that some entries appear twice.\x1b[0m`);
    // Since the problems with the quantity check cost me a lot of time and nerves and drove me to the edge of insanity, I have implemented this ASCII-ART. I hope that it helps developers after me. If you want, you can use a library, which puts the values as an overlay into the ascii art, to prevent the image from shifting.
    console.log(`\n                              Engine                                    LC-Docs                                                             
                                        &@@@@@@@@@@@@@@@@@@@@          .@@@@@@@@@@@@@@@@@@@*                                      
                                  @@@                           @@@@,                          @@@                                
                             /@@                            *@@      %@@                            @@#                           
                          @@,     defined                @@*             @@          documented         @@                        
                        @@         ${sumEntitiesInEngineOnly}                 @&                      @@             ${sumEntitiesInDocsOnly}            (@.                     
                      @,                             @.                       @@                             @.                   
                    @@                             @@                           @                             %@                  
                   @.                             @         intersection         @@                             @                 
                  @                              @    defined and documented      @@                             @                
                 @@                             @#                                 @                              @               
                 @                              @          ${sumEntitiesInEngine-sumEntitiesInEngineOnly} or ${sumEntitiesInDocs-sumEntitiesInDocsOnly}               @@                             @*              
                .@                             &@                                   @                             @@              
                 @                             #@                                  .@                             @@              
                 @                              @                                  @@                             @               
                 (@                             @@                                 @                             @@               
                  @@                             @#                               @                              @                
                   &@                             @@                             @                             &@                 
                     @,                            .@                          @@                             @#                  
                      ,@                             @@                      @@                             @@                    
                         @@                            /@*                 @@                             @&                      
                           ,@@                            @@(           @@                            ,@@                         
                               @@@                            @@%   @@@                           /@@,                            
                                    @@@@*                    ,@@@@ @@@@                     &@@@.                                 
                                             .&@@@@@@@@@%                  *@@@@@@@@@@@/                                          
                                                                                                                                  
                 └────────────────────────────────────────────┘       └────────────────────────────────────────────┘              
                                       ${sumEntitiesInEngine}                                                ${sumEntitiesInDocs}                                       
                                                                                                                                  `);
    // Debug infos
    let array = summaryReportDocs.constants.map(constant => constant.name);
    console.log("Duplicate constants from summary (sum: " + array.length + "): " + array.filter((item, index) => array.indexOf(item) !== index));
    array = summaryReportDocs.functions.map(func => func.name);
    console.log("Duplicate functions from summary (sum: " + array.length + "): " + array.filter((item, index) => array.indexOf(item) !== index));
    array = c4ScriptFunctions;
    console.log("Duplicate c4Script functions (sum: " + array.length + "): " + array.filter((item, index) => array.indexOf(item) !== index));
    array = c4ScriptConstants;
    console.log("Duplicate c4Script constants (sum: " + array.length + "): " + array.filter((item, index) => array.indexOf(item) !== index));
    array = cFunctions.flat();
    console.log("Duplicate .c functions (sum: " + array.length + "): " + array.filter((item, index) => array.indexOf(item) !== index));

    process.exit(2);
}

function readAndFilterFile(filepath, captures) {
    console.log(`Loading ${filepath.replace(/^.*[\\\/]/, '')}`);

    const file = fs.readFileSync(filepath).toString();
    const functions = [];
    captures.forEach(capture => {
        let functionMatcher = capture.regex.exec(file);
                do {
            functions.push(functionMatcher[capture.captureGroup]);
        } while ((functionMatcher = capture.regex.exec(file)) !== null);
    })

    return functions;
}

// Abstract function to find functions and constants separated. entitySuffix can be "()" when used with function names. The parameter "type" is for the console.log generation.
function findEntities(engineEntities, lcdocsEntities, entitySuffix, type) {
    const summary = new Map([['definedOnly', []], ['definedAndDocumented', []], ['documentedOnly', []]])

    // The given array of arrays is an array that contains another array per file. in each file the c4Script functions are contained as string.
    console.log(`\nThe following C4Script ${type} are defined in the engine but not documented in the current lcdocs master:\n`);
    engineEntities.forEach((entityFile, engineEntitiesIndex) => {
        entityFile.forEach((c4ScriptEntityName) => {
            if (!lcdocsEntities.includes(c4ScriptEntityName)) {
                console.log(`\tDefined in ${engineFileNames[engineEntitiesIndex]}: ${c4ScriptEntityName}${entitySuffix}`);
                summary.set('definedOnly', summary.get('definedOnly').concat([c4ScriptEntityName]));
                process.exitCode = 1;
            } else {
                summary.set('definedAndDocumented', summary.get('definedAndDocumented').concat([c4ScriptEntityName]));
            }
        });
    });

    console.log(`\nThe following C4Script ${type} are documented in the current lcdocs master but not defined in the engine:\n`);
    lcdocsEntities.forEach(c4ScriptEntityName => {
        if (!engineEntities.flat().includes(c4ScriptEntityName)) {
            console.log(`\t${c4ScriptEntityName}${entitySuffix}`);
            summary.set('documentedOnly', summary.get('documentedOnly').concat([c4ScriptEntityName]));
        }
    });

    return summary;
}

// ".c"-files can contain functions or constants that overload C4Script.cpp. This function removes duplicates in an array.
function removeOverloadingFunctions(array, c4ScriptCppEntities) {
    let uniqueArray = [];
    array.forEach((element) => {
        if (!c4ScriptCppEntities.includes(element)) {
            uniqueArray.push(element);
        }
    });
    return uniqueArray;
}