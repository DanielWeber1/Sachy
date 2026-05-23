const boardElement = document.getElementById('board');
const statusElement = document.getElementById('statusText');
const announcementElement = document.getElementById('announcement');
const announcementTitleElement = document.getElementById('announcementTitle');
const announcementDetailElement = document.getElementById('announcementDetail');
const roomId = window.location.pathname.startsWith('/room/')
  ? window.location.pathname.slice('/room/'.length)
  : 'room';

const tokenKey = `sachy-token:${roomId}`;
let state = null;
let myColor = null;
let playerToken = window.sessionStorage.getItem(tokenKey) || '';
let selected = null;
let highlightedMoves = [];

const PIECE_IMAGES = {
  white: {
    pawn: '/assets/bily-pesak.png',
    rook: '/assets/bila-vez.png',
    knight: '/assets/bily-kun.png',
    bishop: '/assets/bily-strelec.png',
    queen: '/assets/bila-kralovna.png',
    king: '/assets/bily-kral.png',
  },
  black: {
    pawn: '/assets/cerny-pesak.png',
    rook: '/assets/cerna-vez.png',
    knight: '/assets/cerny-kun.png',
    bishop: '/assets/cerny-strelec.png',
    queen: '/assets/cerna-kralovna.png',
    king: '/assets/cerny-kral.png',
  },
};

function labelForPiece(piece) {
  if (!piece) {
    return '';
  }

  const names = {
    pawn: 'P',
    rook: 'R',
    knight: 'N',
    bishop: 'B',
    queen: 'Q',
    king: 'K',
  };

  return `${piece.color === 'white' ? 'W' : 'B'}${names[piece.type]}`;
}

function pieceImagePath(piece) {
  return piece ? PIECE_IMAGES[piece.color][piece.type] : '';
}

function statusText() {
  if (!state) {
    return 'Connecting...';
  }

  const seatLabel = myColor ? `You are ${myColor}.` : 'Spectator mode.';

  if (state.winner) {
    return `${seatLabel} ${state.winner} wins by checkmate.`;
  }

  if (state.draw) {
    return `${seatLabel} Draw by stalemate.`;
  }

  if (state.check) {
    return `${seatLabel} ${state.turn} is in check.`;
  }

  return `${seatLabel} ${state.turn} to move.`;
}

function setStatus(text) {
  statusElement.textContent = text;
}

function gameOverMessage() {
  if (!state) {
    return null;
  }

  if (state.winner) {
    const winner = state.winner;
    const didWin = myColor && myColor === winner;
    const didLose = myColor && myColor !== winner;
    const detail = didWin
      ? 'Checkmate. You won this game.'
      : didLose
        ? 'Checkmate. You lost this game.'
        : `${winner} won by checkmate.`;

    return {
      title: `${winner.toUpperCase()} WINS`,
      detail,
    };
  }

  if (state.draw) {
    return {
      title: 'DRAW',
      detail: 'Stalemate. No legal moves remain.',
    };
  }

  return null;
}

function renderAnnouncement() {
  if (!announcementElement || !announcementTitleElement || !announcementDetailElement) {
    return;
  }

  const message = gameOverMessage();
  if (!message) {
    announcementElement.hidden = true;
    announcementElement.classList.remove('show');
    return;
  }

  announcementTitleElement.textContent = message.title;
  announcementDetailElement.textContent = message.detail;
  announcementElement.hidden = false;
  announcementElement.classList.add('show');
}

function isSelectedSquare(x, y) {
  return Boolean(selected && selected.x === x && selected.y === y);
}

function isHighlightedSquare(x, y) {
  return highlightedMoves.some((move) => move.toX === x && move.toY === y);
}

function lastMoveTouches(x, y) {
  if (!state?.lastMove) {
    return false;
  }

  const { fromX, fromY, toX, toY } = state.lastMove;
  return (fromX === x && fromY === y) || (toX === x && toY === y);
}

function kingInCheck(x, y) {
  if (!state || !state.check) {
    return false;
  }

  const king = state.board[y][x];
  if (!king || king.type !== 'king') {
    return false;
  }

  return king.color === state.turn;
}

function renderBoard() {
  boardElement.innerHTML = '';

  for (let y = 0; y < 8; y += 1) {
    for (let x = 0; x < 8; x += 1) {
      const square = document.createElement('button');
      square.type = 'button';
      square.className = 'square';
      square.dataset.x = String(x);
      square.dataset.y = String(y);

      square.classList.add((x + y) % 2 === 0 ? 'light' : 'dark');

      if (isSelectedSquare(x, y)) {
        square.classList.add('selected');
      }

      if (isHighlightedSquare(x, y)) {
        square.classList.add('hint');
      }

      if (lastMoveTouches(x, y)) {
        square.classList.add('last-move');
      }

      const currentPiece = state.board[y][x];
      if (kingInCheck(x, y)) {
        square.classList.add('in-check');
      }

      if (currentPiece) {
        const piece = document.createElement('img');
        piece.src = pieceImagePath(currentPiece);
        piece.alt = labelForPiece(currentPiece);
        piece.draggable = false;
        piece.className = 'piece';
        square.appendChild(piece);
      }

      square.addEventListener('click', () => onSquareClick(x, y));
      boardElement.appendChild(square);
    }
  }
}

function render() {
  setStatus(statusText());
  renderBoard();
  renderAnnouncement();
}

async function postForm(url, data) {
  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8',
    },
    body: new URLSearchParams(data).toString(),
  });

  if (!response.ok) {
    const message = await response.json().catch(() => ({}));
    throw new Error(message.error || 'Request failed.');
  }

  return response.json();
}

async function joinRoom() {
  const response = await postForm(`/api/room/${roomId}/join`, { token: playerToken });
  playerToken = response.token;
  myColor = response.color;
  window.sessionStorage.setItem(tokenKey, playerToken);
  state = response.state;
  render();
}

async function loadState() {
  const response = await fetch(`/api/room/${roomId}/state?token=${encodeURIComponent(playerToken)}`);
  if (!response.ok) {
    const message = await response.json().catch(() => ({}));
    throw new Error(message.error || 'Failed to load room state.');
  }

  const payload = await response.json();
  state = payload.state;
  myColor = payload.color;
  render();
}

async function loadMoves(x, y) {
  const response = await fetch(`/api/room/${roomId}/moves?token=${encodeURIComponent(playerToken)}&x=${x}&y=${y}`);
  return response.json();
}

function connectStream() {
  const source = new EventSource(`/api/room/${roomId}/events?token=${encodeURIComponent(playerToken)}`);

  source.onmessage = (event) => {
    const payload = JSON.parse(event.data);
    state = payload.state;
    if (payload.color) {
      myColor = payload.color;
    }

    selected = null;
    highlightedMoves = [];
    render();
  };

  source.onerror = () => {
    setStatus('Reconnecting...');
  };
}

function clearSelection() {
  selected = null;
  highlightedMoves = [];
}

function moveAt(x, y) {
  return highlightedMoves.find((move) => move.toX === x && move.toY === y) || null;
}

async function onSquareClick(x, y) {
  if (!state) {
    return;
  }

  const currentPiece = state.board[y][x];

  if (selected) {
    const targetMove = moveAt(x, y);

    if (targetMove) {
      try {
        await postForm(`/api/room/${roomId}/move`, {
          token: playerToken,
          fromX: String(targetMove.fromX),
          fromY: String(targetMove.fromY),
          toX: String(targetMove.toX),
          toY: String(targetMove.toY),
          promotion: targetMove.promotion || 'queen',
          castle: targetMove.castle || '',
          enPassant: targetMove.enPassant ? 'true' : 'false',
        });

        clearSelection();
        render();
      } catch (error) {
        setStatus(error.message);
        await loadState();
      }

      return;
    }
  }

  if (currentPiece && currentPiece.color === myColor && state.turn === myColor && !state.winner && !state.draw) {
    selected = { x, y };

    try {
      const payload = await loadMoves(selected.x, selected.y);
      highlightedMoves = payload.moves || [];
      render();
    } catch (error) {
      setStatus(error.message);
      clearSelection();
      render();
    }

    return;
  }

  clearSelection();
  render();
}

async function boot() {
  try {
    await joinRoom();
    await loadState();
    connectStream();
  } catch (error) {
    setStatus(error.message);
  }
}

void boot();