(() => {
  const form = document.getElementById('ntfy-form');
  const result = document.getElementById('ntfy-result');
  form.addEventListener('submit', async event => {
    event.preventDefault();
    const submit = form.querySelector('[type=submit]');
    submit.disabled = true;
    result.textContent = '';
    try {
      const response = await fetch('/api/ntfy', {method: 'POST', body: new URLSearchParams(new FormData(form)), signal: AbortSignal.timeout(15000)});
      const text = await response.text();
      if (!response.ok) throw new Error(text);
      result.textContent = 'Sent';
    } catch (error) { result.textContent = error.message; }
    finally { submit.disabled = false; }
  });
})();